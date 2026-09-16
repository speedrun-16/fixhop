#include "engfuncs.h"

#include <cstring>

namespace fixhop
{
    namespace
    {
        // both slots we use must point inside the engine module
        bool slots_plausible(void **table, const code_range &engine)
        {
            if (!table || IsBadReadPtr(table, (get_cvar_pointer_index + 1) * sizeof(void *)))
                return false;

            return engine.contains(table[register_variable_index])
                && engine.contains(table[get_cvar_pointer_index]);
        }

        // prove it by using it. if the indices were wrong this calls an
        // arbitrary engine function, so a fault just means "not this candidate"
        bool validate(void **table)
        {
            __try
            {
                auto get = (get_cvar_pointer_fn)table[get_cvar_pointer_index];
                cvar_t *cv = get("cl_cmdrate");

                if (!cv || IsBadReadPtr(cv, sizeof(cvar_t)))
                    return false;
                
                if (!cv->name || IsBadStringPtrA(cv->name, 64))
                    return false;

                return strcmp(cv->name, "cl_cmdrate") == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        // msvc compiles Initialize's struct copy to a rep movsd whose
        // destination is loaded with mov edi, imm32, and that imm32 is the
        // global we want
        void **from_initialize(HMODULE client, const code_range &client_rng, const code_range &engine)
        {
            BYTE *init = (BYTE *)GetProcAddress(client, "Initialize");

            if (!init)
                return nullptr;

            for (int i = 0; i < 96; i++)
            {
                if (init[i] != 0xBF)
                    continue;

                void **cand = *(void ***)(init + i + 1);
                if (client_rng.contains(cand) && slots_plausible(cand, engine) && validate(cand))
                    return cand;
            }

            return nullptr;
        }

        // the table is a long run of consecutive pointers into the engine image
        // and nothing else in client.dll's data looks like that. section memory
        // is always readable, so probing each slot would only make it slow
        void **from_scan(HMODULE client, const code_range &client_rng, const code_range &engine)
        {
            constexpr int needed = 64;

            auto *dos = (IMAGE_DOS_HEADER *)client;
            auto *nt = (IMAGE_NT_HEADERS *)((BYTE *)client + dos->e_lfanew);
            IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

            for (int s = 0; s < nt->FileHeader.NumberOfSections; s++)
            {
                const DWORD wanted = IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ;
                if ((sec[s].Characteristics & wanted) != wanted)
                    continue;

                auto **start = (void **)(client_rng.base + sec[s].VirtualAddress);
                auto **end = (void **)(client_rng.base + sec[s].VirtualAddress
                                       + (sec[s].Misc.VirtualSize & ~3u));

                void **run = nullptr;
                for (void **p = start; p < end; p++)
                {
                    if (engine.contains(*p))
                    {
                        if (!run)
                            run = p;
                        continue;
                    }

                    // cl_enginefunc_t's first member is pfnSPR_Load, so a run
                    // begins exactly at the table
                    if (run && (p - run) >= needed && slots_plausible(run, engine) && validate(run))
                        return run;

                    run = nullptr;
                }

                if (run && (end - run) >= needed && slots_plausible(run, engine) && validate(run))
                    return run;
            }

            return nullptr;
        }
    }

    code_range module_range(const char *name)
    {
        code_range r;
        HMODULE m = GetModuleHandleA(name);
        if (!m)
            return r;

        auto *dos = (IMAGE_DOS_HEADER *)m;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return r;

        auto *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return r;

        r.base = (BYTE *)m;
        r.size = nt->OptionalHeader.SizeOfImage;
        return r;
    }

    void **find_engfuncs(HMODULE client)
    {
        if (!client)
            return nullptr;

        // hw.dll is the hardware engine, sw.dll the software one
        code_range engine = module_range("hw.dll");
        if (!engine.base)
            engine = module_range("sw.dll");

        if (!engine.base)
            return nullptr;

        code_range client_rng = module_range("client.dll");
        if (!client_rng.base)
            return nullptr;

        if (void **t = from_initialize(client, client_rng, engine))
            return t;

        return from_scan(client, client_rng, engine);
    }
}
