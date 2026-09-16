#pragma once

// enough of the goldsrc playermove_t to reach cmd and Con_NPrintf
//
// copied verbatim from the hlsdk (pm_defs.h, usercmd.h, pmtrace.h): the engine
// passes us its own struct, so every offset has to match. the valve typedef
// spelling is kept on purpose, so this stays diffable against those headers
// rather than matching the rest of our style. fixhop validates the layout at
// runtime before touching anything

typedef float vec3_t[3];
typedef unsigned char byte;
typedef int qboolean;

#define MAX_PHYSENTS        600
#define MAX_MOVEENTS        64
#define MAX_PHYSINFO_STRING 256
#define MAX_MAP_HULLS       4

#define IN_JUMP        (1 << 1)
#define IN_DUCK        (1 << 2)
#define FL_ONGROUND    (1 << 9)
#define FL_DUCKING     (1 << 14)
#define FL_WATERJUMP   (1 << 11)
#define MOVETYPE_WALK  3

typedef struct physent_s
{
    char           name[32];
    int            player;
    vec3_t         origin;
    struct model_s *model;
    struct model_s *studiomodel;
    vec3_t         mins, maxs;
    int            info;
    vec3_t         angles;

    int            solid;
    int            skin;
    int            rendermode;

    float          frame;
    int            sequence;
    byte           controller[4];
    byte           blending[2];

    int            movetype;
    int            takedamage;
    int            blooddecal;
    int            team;
    int            classnumber;

    int            iuser1, iuser2, iuser3, iuser4;
    float          fuser1, fuser2, fuser3, fuser4;
    vec3_t         vuser1, vuser2, vuser3, vuser4;
} physent_t;

typedef struct usercmd_s
{
    short          lerp_msec;
    byte           msec;
    vec3_t         viewangles;

    float          forwardmove;
    float          sidemove;
    float          upmove;
    byte           lightlevel;
    unsigned short buttons;
    byte           impulse;
    byte           weaponselect;

    int            impact_index;
    vec3_t         impact_position;
} usercmd_t;

typedef struct pmplane_s
{
    vec3_t normal;
    float  dist;
} pmplane_t;

typedef struct pmtrace_s
{
    qboolean  allsolid;
    qboolean  startsolid;
    qboolean  inopen, inwater;
    float     fraction;
    vec3_t    endpos;
    pmplane_t plane;
    int       ent;
    vec3_t    deltavelocity;
    int       hitgroup;
} pmtrace_t;

typedef struct playermove_s
{
    int      player_index;
    qboolean server;

    qboolean multiplayer;
    float    time;
    float    frametime;

    vec3_t   forward, right, up;
    vec3_t   origin;
    vec3_t   angles;
    vec3_t   oldangles;
    vec3_t   velocity;
    vec3_t   movedir;
    vec3_t   basevelocity;

    vec3_t   view_ofs;
    float    flDuckTime;
    qboolean bInDuck;

    int      flTimeStepSound;
    int      iStepLeft;

    float    flFallVelocity;
    vec3_t   punchangle;

    float    flSwimTime;
    float    flNextPrimaryAttack;

    int      effects;

    int      flags;
    int      usehull;
    float    gravity;
    float    friction;
    int      oldbuttons;
    float    waterjumptime;
    qboolean dead;
    int      deadflag;
    int      spectator;
    int      movetype;

    int      onground;
    int      waterlevel;
    int      watertype;
    int      oldwaterlevel;

    char     sztexturename[256];
    char     chtexturetype;

    float    maxspeed;
    float    clientmaxspeed;

    int      iuser1, iuser2, iuser3, iuser4;
    float    fuser1, fuser2, fuser3, fuser4;
    vec3_t   vuser1, vuser2, vuser3, vuser4;

    int       numphysent;
    physent_t physents[MAX_PHYSENTS];
    int       nummoveent;
    physent_t moveents[MAX_MOVEENTS];

    int       numvisent;
    physent_t visents[MAX_PHYSENTS];

    usercmd_t cmd;

    int       numtouch;
    pmtrace_t touchindex[MAX_PHYSENTS];

    char      physinfo[MAX_PHYSINFO_STRING];

    struct movevars_s *movevars; // opaque; we never dereference it
    vec3_t    player_mins[MAX_MAP_HULLS];
    vec3_t    player_maxs[MAX_MAP_HULLS];

    const char *(*PM_Info_ValueForKey)(const char *s, const char *key);
    void        (*PM_Particle)(float *origin, int color, float life, int zpos, int zvel);
    int         (*PM_TestPlayerPosition)(float *pos, pmtrace_t *ptrace);
    void        (*Con_NPrintf)(int idx, const char *fmt, ...);

    // the rest of the callback table. only runfuncs is used, the others are
    // here so the offsets land right
    void *Con_DPrintf;
    void *Con_Printf;
    void *Sys_FloatTime;
    void *PM_StuckTouch;
    void *PM_PointContents;
    void *PM_TruePointContents;
    void *PM_HullPointContents;
    void *PM_PlayerTrace;
    void *PM_TraceLine;
    void *RandomLong;
    void *RandomFloat;
    void *PM_GetModelType;
    void *PM_GetModelBounds;
    void *PM_HullForBsp;
    void *PM_TraceModel;
    void *COM_FileSize;
    void *COM_LoadFile;
    void *COM_FreeFile;
    void *memfgets;

    // TRUE only on the first, non-repredicting pass over a given usercmd. The
    // engine uses it to gate one-shot effects (sounds, events) so they do not
    // fire again on every resimulation -- we use it the same way for counting.
    qboolean runfuncs;
    // ...PM_PlaySound and friends follow; we do not need them.
} playermove_t;
