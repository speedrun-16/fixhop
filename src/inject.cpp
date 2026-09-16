#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// minimal x86 LoadLibrary injector
//   inject.exe <process.exe> <dll> [timeout_sec] [--new]
// waits for the target to appear, then runs LoadLibraryA in it

namespace
{
    constexpr int max_ignored = 64;

    // with --new, pids already running when we started are skipped, so a stale
    // instance of the game cannot swallow an injection meant for the one being
    // launched right now
    DWORD find_pid(const char *name, const DWORD *ignored, int num_ignored)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;

        PROCESSENTRY32 pe = {};
        pe.dwSize = sizeof(pe);
        DWORD pid = 0;

        if (Process32First(snap, &pe))
        {
            do
            {
                if (_stricmp(pe.szExeFile, name) != 0)
                    continue;

                bool skip = false;
                for (int i = 0; i < num_ignored && !skip; i++)
                    skip = ignored[i] == pe.th32ProcessID;

                if (!skip)
                {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32Next(snap, &pe));
        }

        CloseHandle(snap);
        return pid;
    }

    int snapshot_pids(const char *name, DWORD *out, int cap)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;

        PROCESSENTRY32 pe = {};
        pe.dwSize = sizeof(pe);
        int n = 0;

        if (Process32First(snap, &pe))
        {
            do
            {
                if (_stricmp(pe.szExeFile, name) == 0 && n < cap)
                    out[n++] = pe.th32ProcessID;
            } while (Process32Next(snap, &pe));
        }

        CloseHandle(snap);
        return n;
    }

    bool already_injected(DWORD pid, const char *dll_name)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snap == INVALID_HANDLE_VALUE)
            return false;

        MODULEENTRY32 me = {};
        me.dwSize = sizeof(me);
        bool found = false;

        if (Module32First(snap, &me))
        {
            do
            {
                if (_stricmp(me.szModule, dll_name) == 0)
                {
                    found = true;
                    break;
                }
            } while (Module32Next(snap, &me));
        }

        CloseHandle(snap);
        return found;
    }

    bool inject(DWORD pid, const char *dll_path)
    {
        HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION
                                  | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                  FALSE, pid);
        if (!proc)
        {
            printf("[inject] OpenProcess failed (%lu), try running as admin\n", GetLastError());
            return false;
        }

        bool ok = false;
        const SIZE_T len = strlen(dll_path) + 1;
        void *remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (!remote)
        {
            printf("[inject] VirtualAllocEx failed (%lu)\n", GetLastError());
        }
        else if (!WriteProcessMemory(proc, remote, dll_path, len, nullptr))
        {
            printf("[inject] WriteProcessMemory failed (%lu)\n", GetLastError());
        }
        else
        {
            // kernel32 loads at the same base in every process of one bitness,
            // so our own LoadLibraryA is valid in the target
            auto loadlib = (LPTHREAD_START_ROUTINE)GetProcAddress(
                GetModuleHandleA("kernel32.dll"), "LoadLibraryA");

            HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadlib, remote, 0, nullptr);
            if (!th)
            {
                printf("[inject] CreateRemoteThread failed (%lu)\n", GetLastError());
            }
            else
            {
                WaitForSingleObject(th, 10000);

                DWORD base = 0;
                GetExitCodeThread(th, &base);
                CloseHandle(th);

                if (base)
                    printf("[inject] injected, module base 0x%08lX\n", base);
                else
                    printf("[inject] LoadLibraryA returned null\n");
                ok = base != 0;
            }
        }

        if (remote)
            VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return ok;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("usage: inject.exe <process.exe> <path\\to.dll> [timeout_sec] [--new]\n");
        return 1;
    }

    const char *proc_name = argv[1];
    int timeout = 120;
    bool only_new = false;

    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--new") == 0)
            only_new = true;
        else
            timeout = atoi(argv[i]);
    }

    char full[MAX_PATH];
    if (!GetFullPathNameA(argv[2], MAX_PATH, full, nullptr)
        || GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES)
    {
        printf("[inject] no such file: %s\n", argv[2]);
        return 1;
    }

    const char *dll_name = strrchr(full, '\\');
    dll_name = dll_name ? dll_name + 1 : full;

    DWORD ignored[max_ignored];
    const int num_ignored = only_new ? snapshot_pids(proc_name, ignored, max_ignored) : 0;

    if (num_ignored)
        printf("[inject] ignoring %d %s already running\n", num_ignored, proc_name);
    printf("[inject] waiting for %s (up to %d s)\n", proc_name, timeout);

    DWORD pid = 0;
    for (int i = 0; i < timeout * 4 && !pid; i++)
    {
        pid = find_pid(proc_name, ignored, num_ignored);
        if (!pid)
            Sleep(250);
    }

    if (!pid)
    {
        printf("[inject] timed out, %s never appeared\n", proc_name);
        return 1;
    }

    printf("[inject] found pid %lu\n", pid);
    Sleep(1500); // let the process finish its own loader work

    if (already_injected(pid, dll_name))
    {
        printf("[inject] %s is already loaded in pid %lu\n", dll_name, pid);
        return 0;
    }

    return inject(pid, full) ? 0 : 1;
}
