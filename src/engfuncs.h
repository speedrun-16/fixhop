#pragma once

#include <windows.h>

// locating the client's engine function table so cvars can be registered
// the engine hands cl_enginefunc_t to client.dll's exported Initialize, which
// copies it into a private global. we do not race that call, we find the copy
// afterwards and refuse to use it until it has proved itself

namespace fixhop
{
    struct cvar_t
    {
        char *name;
        char *string;
        int flags;
        float value;
        cvar_t *next;
    };

    using register_variable_fn = cvar_t *(__cdecl *)(const char *name, const char *value, int flags);
    using get_cvar_pointer_fn = cvar_t *(__cdecl *)(const char *name);
    using con_printf_fn = void(__cdecl *)(const char *fmt, ...);

    // member indices into cl_enginefunc_t, counted in declaration order. the
    // easy mistake is pfnAddCommand's second parameter, itself a function
    // pointer and not a member. index 72 is checked at runtime, which also
    // vouches for the two below it: the only way to miscount is that nested
    // parameter, and getting 72 right means it was handled
    constexpr int register_variable_index = 14;
    constexpr int con_printf_index = 40;
    constexpr int get_cvar_pointer_index = 72;

    // a module's mapped address range, for deciding whether a candidate slot
    // holds a plausible engine code pointer
    struct code_range
    {
        BYTE *base = nullptr;
        SIZE_T size = 0;

        bool contains(const void *p) const {
            return base && (BYTE *)p >= base && (BYTE *)p < base + size;
        }
    };

    code_range module_range(const char *name);

    // the table, or null. never returns an unvalidated candidate
    void **find_engfuncs(HMODULE client);
}
