#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engfuncs.h"
#include "sdk.h"
#include "version.h"

// client side prediction fix for auto bhop
//
// the game stamps the jump velocity from PlayerPreThink, which a vanilla client knows nothing about,
// so it predicts every hop a round trip late
// we reproduce the same write at the same point of the client pipeline, on entry to HUD_PlayerMove

namespace fixhop
{
    // core_feature's default path; a category can override it, and the server
    // publishes the real value per player in physinfo under "jz"
    constexpr float default_jump_z = 250.0f;

    namespace
    {
        using player_move_fn = void(__cdecl *)(playermove_t *pm, int server);
        using frame_fn = void(__cdecl *)(double time);

        // five byte jmp detour, restored around the call instead of trampolined, so no instruction length decoding is needed
        // the client exports we hook are only ever called from the engine's own thread
        struct detour
        {
            BYTE *target = nullptr;
            void *replacement = nullptr;
            BYTE saved[5] = {};
            bool active = false;

            void write()
            {
                BYTE jmp[5];
                jmp[0] = 0xE9;
                *(DWORD *)(jmp + 1) = (DWORD)((BYTE *)replacement - (target + 5));

                memcpy(target, jmp, 5);
                FlushInstructionCache(GetCurrentProcess(), target, 5);
            }

            void restore()
            {
                memcpy(target, saved, 5);
                FlushInstructionCache(GetCurrentProcess(), target, 5);
            }
        };

        // defaults until the console cvars are registered, which happens on the first engine frame
        volatile bool g_enabled = true;
        volatile bool g_debug = false;

        char g_log_path[MAX_PATH] = {};

        void **g_engfuncs = nullptr;
        cvar_t *g_cv_enabled = nullptr;
        cvar_t *g_cv_debug = nullptr;
        cvar_t *g_cv_version = nullptr;
        int g_cvar_attempts = 0;
        bool g_bootstrapped = false;

        HMODULE g_client = nullptr;
        detour g_move;  // client.dll!HUD_PlayerMove, fix itself
        detour g_frame; // client.dll!HUD_Frame, one shot, runs at the menu too

        // the struct offsets are ours, the struct is the engine's, so nothing is touched until the engine has proved them right
        constexpr int layout_required = 10;
        int g_layout_good = 0;
        bool g_layout_ok = false;

        volatile float g_jump_z = default_jump_z;
        const char *g_jump_z_src = "default";
        bool g_from_physinfo = false;

        unsigned g_hops = 0;
        bool g_was_jumping = false;

        void log_line(const char *fmt, ...)
        {
            char buf[512];
            va_list ap;
            va_start(ap, fmt);
            _vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
            va_end(ap);
            buf[sizeof(buf) - 2] = 0;
            strcat(buf, "\n");

            OutputDebugStringA(buf);

            // timestamped, because the log spans sessions and the only thing
            // that makes an old line distinguishable from this run is the clock
            SYSTEMTIME t;
            GetLocalTime(&t);

            if (FILE *f = fopen(g_log_path, "a"))
            {
                fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
                fputs(buf, f);
                fclose(f);
            }
        }

        // CL_SetupPMove fills frametime from cmd.msec before calling us, and cmd
        // sits behind mb of physent arrays, so that identity cannot hold if any offset is wrong
        void validate_layout(const playermove_t *pm)
        {
            const bool sane = pm->player_index >= 0 && pm->player_index < 33
                           && pm->movetype >= 0 && pm->movetype <= 12
                           && pm->cmd.msec <= 255
                           && (pm->runfuncs == 0 || pm->runfuncs == 1)
                           && pm->frametime == pm->cmd.msec / 1000.0f;

            if (!sane)
            {
                if (g_layout_good)
                    log_line("[fixhop] layout check reset (idx=%d mt=%d msec=%u ft=%.6f)",
                             pm->player_index, pm->movetype, pm->cmd.msec, pm->frametime);

                g_layout_good = 0;
                return;
            }

            if (++g_layout_good >= layout_required)
            {
                g_layout_ok = true;
                log_line("[fixhop] playermove_t layout validated, fix is live");
            }
        }

        // fixhop_version is detection probe
        // fixhop_enabled and fixhop_debug are live toggles
        // registered from inside the hook, on the engine's own thread: cvar registration walks a global list
        void ensure_cvars()
        {
            constexpr int max_attempts = 20;
            if (g_engfuncs || g_cvar_attempts >= max_attempts)
                return;

            g_engfuncs = find_engfuncs(GetModuleHandleA("client.dll"));
            if (!g_engfuncs)
            {
                if (++g_cvar_attempts == max_attempts)
                    log_line("[fixhop] no cl_enginefunc_t, console cvars unavailable and the "
                             "server cannot detect this client (the movement fix still works)");
                return;
            }

            // the engine keeps the name and value pointers, so string literals
            auto reg = (register_variable_fn)g_engfuncs[register_variable_index];

            g_cv_version = reg("fixhop_version", build_version, 0);
            g_cv_enabled = reg("fixhop_enabled", "1", 0);
            g_cv_debug = reg("fixhop_debug", "0", 0);

            log_line("[fixhop] registered fixhop_version=%s, fixhop_enabled, fixhop_debug "
                     "(engfuncs at %p)", build_version, (void *)g_engfuncs);
        }

        void read_cvars()
        {
            if (g_cv_enabled)
                g_enabled = (g_cv_enabled->value != 0.0f);

            if (g_cv_debug)
                g_debug = (g_cv_debug->value != 0.0f);
        }

        // not pm->onground: that field is seeded from entity_state_t,
        // which cstrike does not delta, so it arrives as 0 meaning "on entity 0", the world
        // flags is rewritten by PM_Move every command and is the field
        // CBasePlayer::Jump itself tests
        inline bool on_ground(const playermove_t *pm)
        {
            return (pm->flags & FL_ONGROUND) != 0;
        }

        // hand rolled rather than calling pmove->PM_Info_ValueForKey, so the hook
        // never calls back into the engine
        float infostring_float(const char *info, const char *key, float fallback)
        {
            const size_t klen = strlen(key);

            for (const char *p = info; *p == '\\'; )
            {
                const char *k = ++p;
                while (*p && *p != '\\')
                    p++;
                const size_t kl = (size_t)(p - k);
                if (*p != '\\')
                    break;

                const char *v = ++p;
                while (*p && *p != '\\')
                    p++;

                if (kl != klen || strncmp(k, key, klen) != 0)
                    continue;

                char buf[32];
                size_t vl = (size_t)(p - v);
                if (vl >= sizeof(buf))
                    vl = sizeof(buf) - 1;
                memcpy(buf, v, vl);
                buf[vl] = 0;

                const float f = (float)atof(buf);
                return f > 0.0f ? f : fallback;
            }
            
            return fallback;
        }

        void set_jump_z(float z, const char *src, bool physinfo)
        {
            if (z < 50.0f || z > 1000.0f)
                return;

            if (g_jump_z == z && g_from_physinfo == physinfo)
                return;

            g_jump_z = z;
            g_jump_z_src = src;
            g_from_physinfo = physinfo;
            log_line("[fixhop] server jump z = %.4f (%s)", z, src);
        }

        // prediction is seeded from entity_state_t, which does not carry oldbuttons,
        // so every resimulation pass starts believing nothing was held last command
        // a held duck then looks like a fresh press every render frame and PM_Duck restarts the duck ramp
        // we cannot know the real previous buttons, but we can infer these two:
        // both conditions are only reachable if the press already happened
        void fix_oldbuttons_seed(playermove_t *pm)
        {
            const int buttons = pm->cmd.buttons;

            if ((buttons & IN_DUCK) && (pm->bInDuck || (pm->flags & FL_DUCKING)))
                pm->oldbuttons |= IN_DUCK;

            // without this the seed pass grants a phantom hop the server never
            // gave, capped by PM_PreventMegaBunnyJumping
            if ((buttons & IN_JUMP) && !on_ground(pm))
                pm->oldbuttons |= IN_JUMP;
        }

        // mirror of the game dll's regame_api.cpp on_jump. it is reached only
        // when PlayerPreThink sees IN_JUMP, and does not check duck, so a ducked
        // player hops like a standing one
        void apply_prethink_jump(playermove_t *pm)
        {
            const bool jumping =
                   !pm->server // listenserver: the game dll already did it
                && !pm->dead && !pm->deadflag
                && !pm->spectator && pm->iuser1 <= 0
                && pm->movetype == MOVETYPE_WALK
                && (pm->cmd.buttons & IN_JUMP)
                && on_ground(pm)
                && !(pm->flags & FL_WATERJUMP)
                && pm->waterjumptime == 0.0f
                && pm->waterlevel < 2;

            if (jumping)
                pm->velocity[2] = g_jump_z;

            // runfuncs is true once per command, so this counts hops and not the
            // nine or so resimulation passes each command gets at 90 ping
            if (pm->runfuncs)
            {
                if (jumping && !g_was_jumping)
                    g_hops++;

                g_was_jumping = jumping;
            }
        }

        // cvars and the console banner
        // driven from HUD_Frame, which the engine calls every frame from the main menu onward,
        // so both are up before the player joins anything
        void bootstrap()
        {
            if (g_bootstrapped)
                return;

            ensure_cvars();
            if (!g_engfuncs)
                return;

            code_range engine = module_range("hw.dll");
            if (!engine.base)
                engine = module_range("sw.dll");

            if (engine.contains(g_engfuncs[con_printf_index]))
            {
                auto con_printf = (con_printf_fn)g_engfuncs[con_printf_index];
                con_printf("fixhop %s (%s) loaded\n", build_version, build_commit);
                con_printf("%s\n", repo_url);
            }

            g_bootstrapped = true;
        }

        void draw_overlay(const playermove_t *pm)
        {
            pm->Con_NPrintf(0, "fixhop: %s   jump z %.2f (%s)   hops %u",
                            g_enabled ? "ON" : "off", g_jump_z, g_jump_z_src, g_hops);
                            
            pm->Con_NPrintf(1, "ground %d  duck %d%d  vel %.0f  z %.0f",
                            on_ground(pm) ? 1 : 0,
                            pm->bInDuck ? 1 : 0,
                            (pm->flags & FL_DUCKING) ? 1 : 0,
                            (float)sqrt(pm->velocity[0] * pm->velocity[0] +
                                        pm->velocity[1] * pm->velocity[1]),
                            pm->velocity[2]);
        }

        // one shot: the bootstrap is all this is for, so it unhooks itself once
        // that is done rather than costing a detour on every rendered frame
        void __cdecl hooked_frame(double time)
        {
            bootstrap();

            g_frame.restore();
            ((frame_fn)g_frame.target)(time);

            if (g_bootstrapped)
                g_frame.active = false;
            else
                g_frame.write();
        }

        void __cdecl hooked_player_move(playermove_t *pm, int server)
        {
            if (pm && !g_layout_ok)
            {
                validate_layout(pm);
            }
            else if (pm)
            {
                if (pm->runfuncs)
                {
                    bootstrap(); // in case HUD_Frame could not be hooked
                    read_cvars();

                    // physinfo is per player and networked, so it follows the
                    // player's active category
                    const float jz = infostring_float(pm->physinfo, "jz", 0.0f);
                    if (jz > 0.0f)
                        set_jump_z(jz, "physinfo", true);
                    else if (g_from_physinfo)
                        set_jump_z(default_jump_z, "default", false);
                }

                if (g_enabled)
                {
                    fix_oldbuttons_seed(pm);
                    apply_prethink_jump(pm);
                }
            }

            g_move.restore();
            ((player_move_fn)g_move.target)(pm, server);
            g_move.write();

            if (pm && g_layout_ok && g_debug && pm->Con_NPrintf && !pm->server)
                draw_overlay(pm);
        }

        bool attach_detour(detour &d, HMODULE mod, const char *name, void *replacement)
        {
            void *fn = (void *)GetProcAddress(mod, name);
            if (!fn)
            {
                log_line("[fixhop] client.dll has no %s export", name);
                return false;
            }

            DWORD old;
            if (!VirtualProtect(fn, 5, PAGE_EXECUTE_READWRITE, &old))
            {
                log_line("[fixhop] VirtualProtect on %s failed (%lu)", name, GetLastError());
                return false;
            }

            d.target = (BYTE *)fn;
            d.replacement = replacement;
            memcpy(d.saved, d.target, 5);
            d.write();
            d.active = true;

            log_line("[fixhop] hooked client.dll!%s at %p", name, fn);
            return true;
        }

        bool install()
        {
            HMODULE cl = GetModuleHandleA("client.dll");
            if (!cl)
                return false;

            if (!attach_detour(g_move, cl, "HUD_PlayerMove", (void *)&hooked_player_move))
                return false;

            g_client = cl;
            g_layout_ok = false;
            g_layout_good = 0;
            g_hops = 0;
            g_was_jumping = false;

            // the bootstrap rides HUD_Frame so the cvars and the banner are up
            // at the main menu. without it they wait for the first map, which
            // looks exactly like the injection having failed
            attach_detour(g_frame, cl, "HUD_Frame", (void *)&hooked_frame);

            log_line("[fixhop] the fix goes live on the first PM_PlayerMove");
            return true;
        }

        DWORD WINAPI worker(LPVOID)
        {
            for (;;)
            {
                HMODULE cl = GetModuleHandleA("client.dll");

                // unloaded or reloaded, so the saved bytes and target are stale
                if (g_move.active && cl != g_client)
                {
                    log_line("[fixhop] client.dll changed (%p -> %p), re-hooking", g_client, cl);
                    g_move = detour{};
                    g_frame = detour{};
                    g_engfuncs = nullptr;
                    g_cvar_attempts = 0;
                    g_bootstrapped = false;
                }

                if (!g_move.active && cl)
                    install();

                Sleep(500);
            }
        }
    }

    void attach(HINSTANCE inst)
    {
        // miles frees a .asi that registers no codec. the hook has to outlive
        // that, so pin the module before it can happen
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)inst, &self);


        // log beside the module, wherever it was loaded from
        char dir[MAX_PATH] = {};
        GetModuleFileNameA(inst, dir, sizeof(dir));
        if (char *slash = strrchr(dir, '\\'))
            *(slash + 1) = 0;

        _snprintf(g_log_path, sizeof(g_log_path) - 1, "%sfixhop.log", dir);

        log_line("[fixhop] attached to pid %lu", GetCurrentProcessId());
        CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        fixhop::attach(inst);
    }
    return TRUE;
}
