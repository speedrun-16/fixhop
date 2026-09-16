#include <windows.h>

// loading as a .asi out of the half-life directory
//
// mss32.dll loads every *.asi in the game directory looking for audio codecs,
// so giving it that extension is enough to get it loaded at startup
//
// in return it calls one entry point, "_RIB_Main@20" or the undecorated
// "RIB_Main". we export it and register nothing. __stdcall is not optional:
// miles pushes five arguments and expects the callee to clean them up

extern "C" int __stdcall RIB_Main(void *, void *, void *, void *, void *)
{
    return 0;
}

// both spellings, miles accepts either
#pragma comment(linker, "/EXPORT:_RIB_Main@20")
#pragma comment(linker, "/EXPORT:RIB_Main=_RIB_Main@20")
