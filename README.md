# fixhop

Auto bunnyhop client side prediction fix for counter-strike 1.6

## How it works

Auto bhop applied server side from `PlayerPreThink` is invisible to the client,
which keeps predicting you grounded until the snapshot carrying the real hop
lands a round trip later, so every jump starts with a lag.

fixhop writes the same jump velocity on entry to `HUD_PlayerMove`, the point in the client
pipeline where `PlayerPreThink` sits on the server so prediction matches at any ping

it also puts back the `oldbuttons` bits the server never sends, which is what makes a held duck flicker.

The write works because `pm_shared` is the same code on both sides and
`velocity[2]` on entry is the only thing that differs:

```c
PM_CategorizePosition();              // velocity[2] > 180 sets onground = -1
if (buttons & IN_JUMP) PM_Jump();     // airborne: no op, grounded: jump not released, no op
if (onground != -1) {
    velocity[2] = 0;                  // where an unpredicted hop dies
    PM_Friction();
}
```

## Build

Prebuilt archives are published to the rolling
[nightly release](https://github.com/speedrun-16/fixhop/releases/tag/nightly),
and tagged ones whenever `VERSION` in `CMakeLists.txt` changes. The same
artifacts hang off every successful GitHub Actions run:

| archive | contents |
|---|---|
| `fixhop-asi.zip` | `fixhop.asi`, the drop in route |
| `fixhop-injector.zip` | `fixhop.dll`, `inject.exe` and `run.bat` |

To build it yourself you need the 32 bit MSVC toolset, since `hl.exe` is x86.

```
cmake -S . -B build -A Win32
cmake --build build --config Release
```

## Run

Drop `fixhop.asi` next to `hl.exe`. \
I'd suggest to play with `-insecure` parameter. \
The other route is `run.bat`, which passes that for you and injects
`fixhop.dll` into the client instead.

Console cvars:

| cvar | |
|---|---|
| `fixhop_version` | version, and how a server detects the fix |
| `fixhop_enabled` | live toggle |
| `fixhop_debug` | overlay plus `fixhop.log` |

`fixhop.log`, written beside the module, should report `layout validated` once
you are in a map. If it never does, the struct offsets did not check out and
nothing was touched.

## Server admins

fixhop assumes a jump velocity of 250, which is what the speedrun mod uses. If
yours differs, publish it in physinfo under `jz` and connected clients pick it
up:

```
engfunc(EngFunc_SetPhysicsKeyValue, id, "jz", "250.0")       // amxx
g_engfuncs.pfnSetPhysicsKeyValue(edict, "jz", "250.0");      // metamod
```

Set it on connect and whenever it changes. It is per player, so per class or per
mode heights work.

If your auto bhop runs the engine's own `PM_Jump` instead (ReGameDLL's
`sv_autobunnyhopping`) the value is `sqrt(2 * 800 * mp_jump_height)`, 268 by default.

To detect the fix query `fixhop_version` with `pfnQueryClientCvarValue2`. \
Client without it answers `Bad CVAR request`.

## Author

[PWNED](https://github.com/5z3f)
