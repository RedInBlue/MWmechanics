// CarAudioRate.asi - run NFS Carbon's car audio logic at a fixed rate, independent of the frame rate.
//
// NFSC.exe v1.4 only (7,217,152 bytes); every patch site is verified before writing.
// Build (32-bit MinGW):
//   i686-w64-mingw32-g++ -shared -O2 -static -s -o CarAudioRate.asi CarAudioRate.cpp
//
// Why: each car's audio state (EAXCar) runs its controllers (engine, shifting, accel transition, ...)
// once per game update with the frame time. Several of them step once per call instead of per
// second, so with WidescreenFix SimRate they behave differently at 144 fps than at 60:
// - the post-shift LFO (per-car shiftpattern frequency and amplitude) is sampled once per update,
//   so its audible wobble depends on the update rate;
// - torque and visual RPM are 3- and 2-sample moving averages;
// - Carbon adds an RPM blend of 0.95 old + 0.05 new per update (SFXCTL_Engine::UpdateRPM 0x510ECA);
// - the clutch model and redline bounce use smooth() with a fixed step per call.
//
// Fix: the UpdateParams slot (+0x14) of the five EAXCar vtables goes through a wrapper that adds up
// the frame time per car and calls the original only once `1 / Rate` has passed, with whole steps
// (the remainder carries over). At or below `Rate` fps every frame updates as before. ProcessUpdate,
// which pushes the values to the sound engine, still runs every frame and only re-sends them; shift
// sounds are triggered inside UpdateParams, so they fire once either way.
//
// Side effect: the audio RPM (MWTachPatch UseAudioRPM) updates at `Rate` too, so the tach needle
// moves in 60 Hz steps. MWTachPatch's SmoothRedlineShake sees dt = 1 / 60 and keeps its MW timing.

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#define THISCALL __attribute__((thiscall))

typedef void(THISCALL *Update_t)(void *car, float t);

// UpdateParams slot in each EAXCar vtable (every vtable with EAXCar::ProcessUpdate 0x53B730 at +0x18)
struct CarVtable {
    uintptr_t slot, func;
};

static const CarVtable kCars[] = {
    {0x9CDB4C, 0x53B750},  // EAXCar
    {0x9CB58C, 0x5034B0},  // player car (then updates POV and rotation)
    {0x9CBA64, 0x503200},
    {0x9CBB04, 0x517590},
    {0x9CBAB4, 0x5175C0},
};

static float gStep = 1.0f / 60.0f;

// Time not yet given to each car; cars come and go, so slots are reused round robin
struct Clock {
    void *car;
    float pending;
};

static Clock gClocks[32];
static int gNextClock;

static void Step(void *car, float t, Update_t update) {
    if (!(t > 0.0f)) {  // paused: pass through as before
        update(car, t);
        return;
    }
    Clock *c = nullptr;
    for (Clock &k : gClocks)
        if (k.car == car) c = &k;
    if (!c) {
        c = &gClocks[gNextClock++ % 32];
        c->car = car;
        c->pending = gStep;  // first update right away
    }
    c->pending += t;
    if (c->pending < gStep) return;
    float steps = floorf(c->pending / gStep);
    c->pending -= steps * gStep;
    update(car, steps * gStep);
}

template <int I>
static void THISCALL FixedUpdate(void *car, float t) {
    Step(car, t, (Update_t)kCars[I].func);
}

static void *const kHooks[] = {(void *)&FixedUpdate<0>, (void *)&FixedUpdate<1>, (void *)&FixedUpdate<2>,
                               (void *)&FixedUpdate<3>, (void *)&FixedUpdate<4>};

static void Install(const char *ini) {
    for (const CarVtable &v : kCars)
        if (*(const uintptr_t *)v.slot != v.func) return;

    char buf[32];
    GetPrivateProfileStringA("CarAudioRate", "Rate", "60", buf, sizeof(buf), ini);
    float rate = strtof(buf, nullptr);
    if (rate <= 0.0f) return;
    if (rate < 10.0f) rate = 10.0f;
    gStep = 1.0f / rate;

    for (int i = 0; i < 5; i++) {
        DWORD old;
        VirtualProtect((void *)kCars[i].slot, 4, PAGE_READWRITE, &old);
        *(void **)kCars[i].slot = kHooks[i];
        VirtualProtect((void *)kCars[i].slot, 4, old, &old);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char ini[MAX_PATH];
        GetModuleFileNameA(module, ini, MAX_PATH);
        char *dot = strrchr(ini, '.');
        if (dot) strcpy(dot, ".ini");
        Install(ini);
    }
    return TRUE;
}
