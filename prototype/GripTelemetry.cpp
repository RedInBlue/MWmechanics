// GripTelemetry.asi - logs player-car handling data to CSV in NFS Most Wanted (speed.exe v1.3)
// and NFS Carbon (NFSC.exe v1.4), so both games can be compared on the same car and corner.
//
// Build (32-bit MinGW):
//   i686-w64-mingw32-g++ -shared -O2 -static -s -o GripTelemetry.asi GripTelemetry.cpp
//
// How it works: SuspensionRacer::OnTaskSimulate calls Chassis::ComputeLateralGripScale(state)
// once per physics tick, just before Tire::BeginFrame. We redirect that call, run the original,
// and (for the human-driven car only) write one CSV row with the chassis state, the grip scale
// the game produced, steering/drift/burnout state, and each tire's state from the previous tick.
//
// F10 starts/stops a recording. Each recording is a new CSV next to the .asi.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

#define THISCALL __attribute__((thiscall))

typedef float(THISCALL *GripScale_t)(void *self, const void *state);

struct TireOffsets {
    int load, latForce, longForce, traction, slipAngle, gripBoost, tractionBoost, driftFriction, roadSpeed;
    int av, slip, driveTorque, radius;
    int ellipseFlag, ellipseLong, ellipseLat;  // Carbon only, -1 = n/a
};

struct RacerOffsets {
    int steerPrevious, steerWheelL, steerWheelR, steerLastMax, yawControl;
    int driftState, driftValue, burnoutState, burnoutTraction;
    int tireSpecs, yawSpeed;                     // [this+tireSpecs] -> tires layout, YAW_SPEED field
    int chassisSpecs, pitchUp, pitchDown, roll;  // Carbon-only torque scalers in DoWheelForces, -1 = n/a
};

struct GameInfo {
    const char *name;
    uintptr_t callSite;         // in SuspensionRacer::OnTaskSimulate (player physics)
    uintptr_t gripScaleFn;
    uintptr_t latForceMultPtr;  // pLatForceMultipliers (DRM-protected, used in ComputeLateralForce)
    int tiresOffset;            // SuspensionRacer::mTires[4]
    TireOffsets tire;
    RacerOffsets racer;
    // Per-racer race log (Carbon only, 0 = off): second grip-scale call site (SuspensionSimple),
    // and the AIPerpVehicle ICheater::GetCatchupCheat vtable slot + original function.
    uintptr_t simpleCallSite;
    uintptr_t cheatSlot, cheatFn;
};

// Offsets from MW decomp (SuspensionRacer, PC layout = console + 8) and matching Carbon disassembly
static GameInfo kGames[] = {
    {"MW", 0x6B1F79, 0x68D5F0, 0x8F8AA8, 0x1B4,
     {0xD4, 0xD8, 0xDC, 0x110, 0x118, 0x128, 0xEC, 0x12C, 0xFC, 0xD0, 0xF0, 0xE0, 0xC4, -1, -1, -1},
     {0x11C, 0x120, 0x124, 0x12C, 0x1B0, 0x104, 0x108, 0x10C, 0x114, 0xA0, 0x4C, -1, -1, -1, -1}, 0, 0, 0},
    {"Carbon", 0x703C05, 0x6DE890, 0xA6358C, 0x1C0,
     {0xD0, 0xD4, 0xD8, 0x10C, 0x114, 0x11C, 0xE8, 0x120, 0xF8, 0xCC, 0xEC, 0xDC, 0xC0, 0x124, 0x128, 0x12C},
     {0x104, 0x108, 0x10C, 0x114, 0x198, 0xEC, 0xF0, 0xF4, 0xFC, 0x90, 0x88, 0xB0, 0x7C, 0x6C, 0x54}, 0x7046E8, 0x9C4ABC, 0x40AE80},
};

// Per-car state for the race log, keyed by the car's COM object (shared by all its interfaces:
// [ISimable+4] from the suspension's owner, [ICheater+4] from the catch-up hook).
struct CarRec {
    const void *key;
    int id;
    double dist, lastLog, cheatTime;
    float baseSkill, glueSkill, cheat, cheatOverride;
};
static CarRec gCars[64];
static int gNumCars = 0;
static FILE *gRaceFile = nullptr;
static double gPlayerDist = 0.0;

static CarRec *FindCar(const void *key) {
    for (int i = 0; i < gNumCars; i++)
        if (gCars[i].key == key) return &gCars[i];
    if (gNumCars >= 64) return nullptr;
    CarRec &c = gCars[gNumCars];
    c = CarRec{key, gNumCars, 0.0, -1.0, -1.0, NAN, NAN, NAN, NAN};
    gNumCars++;
    return &c;
}

static GameInfo *gGame = nullptr;
static char gDir[MAX_PATH];
static FILE *gFile = nullptr;
static double gTime = 0.0;
static float gPrevVel[3];
static bool gHavePrev = false;
static bool gKeyWasDown = false;
static int gVKey = VK_F10;

static inline float F(const void *p, int off) { return *(const float *)((const char *)p + off); }
static inline int I(const void *p, int off) { return *(const int *)((const char *)p + off); }

static float SpecField(const void *self, int ptrOff, int fieldOff) {
    if (ptrOff < 0) return NAN;
    const char *layout = *(const char *const *)((const char *)self + ptrOff);
    return layout ? F(layout, fieldOff) : NAN;
}

static void StartRecording() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sgrip_%s_%04d%02d%02d_%02d%02d%02d.csv", gDir, gGame->name, st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    gFile = fopen(path, "w");
    if (!gFile) return;
    setvbuf(gFile, nullptr, _IOFBF, 1 << 16);
    fprintf(gFile, "game,t,dt,speed_mph,gas,brake,ebrake,steer,car_slip_deg,yaw_rate,lat_g,long_g,gear,driver_style,flags,"
                   "grip_scale,latforce_mult,steer_deg,steer_max_deg,wheel_l,wheel_r,yaw_control,drift_state,drift_value,"
                   "burnout_state,burnout_traction,yaw_speed,pitch_neg_scale,pitch_pos_scale,roll_scale");
    static const char *names[4] = {"fl", "fr", "rl", "rr"};
    for (auto n : names)
        fprintf(gFile,
                ",%s_load,%s_lat,%s_long,%s_traction,%s_slip_deg,%s_gripboost,%s_tractboost,%s_driftfric,%s_ellipse,%s_ell_long,"
                "%s_ell_lat,%s_wheel_mps,%s_road_mps,%s_slip_mps,%s_drive_torque",
                n, n, n, n, n, n, n, n, n, n, n, n, n, n, n);
    fprintf(gFile, "\n");
    gTime = 0.0;
    gHavePrev = false;

    if (gGame->simpleCallSite) {
        snprintf(path, sizeof(path), "%srace_%s_%04d%02d%02d_%02d%02d%02d.csv", gDir, gGame->name, st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
        gRaceFile = fopen(path, "w");
        if (gRaceFile) {
            setvbuf(gRaceFile, nullptr, _IOFBF, 1 << 16);
            fprintf(gRaceFile, "t,car,driver_class,physics,speed_mph,dist_m,gap_m,x,y,z,gas,brake,gear,base_skill,glue_skill,"
                               "catchup_cheat,cheat_override,cheat_age\n");
        }
        for (int i = 0; i < gNumCars; i++) {
            gCars[i].dist = 0.0;
            gCars[i].lastLog = -1.0;
        }
        gPlayerDist = 0.0;
    }
    Beep(1200, 80);
}

static void StopRecording() {
    if (gRaceFile) {
        fclose(gRaceFile);
        gRaceFile = nullptr;
    }
    if (gFile) {
        fclose(gFile);
        gFile = nullptr;
        Beep(600, 80);
    }
}

// One row per car every 0.05 s of player time. Distances are integrated from each car's own speed
// since F10, so gap_m = car distance - player distance (positive = ahead) if recording starts at the line.
static void RaceTick(void *self, const char *s, int physics) {
    short cls = *(const short *)(s + 0xCA);
    if (cls != 0 && cls != 3) return;  // DRIVER_HUMAN, DRIVER_RACER
    const char *owner = *(const char *const *)((const char *)self + 0x2C);
    CarRec *c = FindCar(owner ? *(const void *const *)(owner + 4) : self);
    if (!c) return;
    c->dist += F(s, 0xBC) * F(s, 0xC0);
    if (cls == 0) gPlayerDist = c->dist;
    if (!gRaceFile || gTime - c->lastLog < 0.05) return;
    c->lastLog = gTime;
    fprintf(gRaceFile, "%.3f,%d,%d,%d,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%d,%.3f,%.3f,%.4f,%.4f,%.2f\n", gTime, c->id, cls, physics,
            F(s, 0xBC) * 2.23694f, c->dist, c->dist - gPlayerDist, F(s, 0x30), F(s, 0x34), F(s, 0x38), F(s, 0x4C), F(s, 0x5C),
            *(const short *)(s + 0xCC), c->baseSkill, c->glueSkill, c->cheat, c->cheatOverride,
            c->cheatTime < 0 ? -1.0 : gTime - c->cheatTime);
}

static void PollHotkey() {
    bool down = (GetAsyncKeyState(gVKey) & 0x8000) != 0;
    if (down && !gKeyWasDown) {
        if (gFile) StopRecording();
        else StartRecording();
    }
    gKeyWasDown = down;
}

static void LogRow(void *self, const char *s, float gripScale) {
    float dt = F(s, 0xC0);
    if (dt <= 0.0f) return;
    gTime += dt;

    // Acceleration in the car's frame from the change in world velocity
    const float *right = (const float *)(s + 0x00);
    const float *fwd = (const float *)(s + 0x20);
    const float *vel = (const float *)(s + 0x50);
    float latG = 0.0f, longG = 0.0f;
    if (gHavePrev) {
        float a[3] = {(vel[0] - gPrevVel[0]) / dt, (vel[1] - gPrevVel[1]) / dt, (vel[2] - gPrevVel[2]) / dt};
        latG = (a[0] * right[0] + a[1] * right[1] + a[2] * right[2]) / 9.81f;
        longG = (a[0] * fwd[0] + a[1] * fwd[1] + a[2] * fwd[2]) / 9.81f;
    }
    memcpy(gPrevVel, vel, sizeof(gPrevVel));
    gHavePrev = true;

    int latMult = 0;
    int *multObj = *(int **)gGame->latForceMultPtr;
    if (multObj) latMult = multObj[2];

    fprintf(gFile, "%s,%.4f,%.5f,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%.4f,%.4f,%.4f,%d,%d,%d,%.4f,%d", gGame->name, gTime, dt,
            F(s, 0xBC) * 2.23694f, F(s, 0x4C), F(s, 0x5C), F(s, 0x7C), F(s, 0x8C), F(s, 0x9C) * 360.0f, F(s, 0x94), latG, longG,
            *(const short *)(s + 0xCC), *(const short *)(s + 0xC8), *(const int *)(s + 0xC4), gripScale, latMult);

    const RacerOffsets &r = gGame->racer;
    fprintf(gFile, ",%.3f,%.3f,%.4f,%.4f,%.4f,%d,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f", F(self, r.steerPrevious), F(self, r.steerLastMax),
            F(self, r.steerWheelL), F(self, r.steerWheelR), F(self, r.yawControl), I(self, r.driftState), F(self, r.driftValue),
            I(self, r.burnoutState), F(self, r.burnoutTraction), SpecField(self, r.tireSpecs, r.yawSpeed),
            SpecField(self, r.chassisSpecs, r.pitchUp), SpecField(self, r.chassisSpecs, r.pitchDown),
            SpecField(self, r.chassisSpecs, r.roll));

    const TireOffsets &o = gGame->tire;
    for (int i = 0; i < 4; i++) {
        const char *t = *(const char **)((const char *)self + gGame->tiresOffset + i * 4);
        if (!t) {
            fprintf(gFile, ",,,,,,,,,,,,,,,");
            continue;
        }
        fprintf(gFile, ",%.1f,%.1f,%.1f,%.4f,%.3f,%.4f,%.4f,%.4f", F(t, o.load), F(t, o.latForce), F(t, o.longForce),
                F(t, o.traction), F(t, o.slipAngle) * 360.0f, F(t, o.gripBoost), F(t, o.tractionBoost), F(t, o.driftFriction));
        if (o.ellipseFlag >= 0)
            fprintf(gFile, ",%d,%.4f,%.4f", *(const uint8_t *)(t + o.ellipseFlag), F(t, o.ellipseLong), F(t, o.ellipseLat));
        else
            fprintf(gFile, ",,,");
        fprintf(gFile, ",%.3f,%.3f,%.3f,%.1f", F(t, o.av) * F(t, o.radius), F(t, o.roadSpeed), F(t, o.slip), F(t, o.driveTorque));
    }
    fprintf(gFile, "\n");
}

static float THISCALL HookGripScale(void *self, const void *state) {
    float result = ((GripScale_t)gGame->gripScaleFn)(self, state);
    const char *s = (const char *)state;
    if (*(const short *)(s + 0xCA) == 0) {  // DRIVER_HUMAN
        PollHotkey();
        if (gFile) LogRow(self, s, result);
    }
    if (gGame->simpleCallSite) RaceTick(self, s, 1);
    return result;
}

// Same call from the simplified suspension (AI cars). Only feeds the race log.
static float THISCALL HookGripScaleSimple(void *self, const void *state) {
    float result = ((GripScale_t)gGame->gripScaleFn)(self, state);
    RaceTick(self, (const char *)state, 2);
    return result;
}

typedef float(THISCALL *Cheat_t)(void *self);

// AIPerpVehicle's ICheater::GetCatchupCheat (called by the AI engine each tick). Records the
// catch-up inputs: [this+0x40] base skill, [this+0x44] glue skill, [this+0x54] Carbon override.
static float THISCALL HookCheat(void *self) {
    float result = ((Cheat_t)gGame->cheatFn)(self);
    CarRec *c = FindCar(*(const void *const *)((const char *)self + 4));
    if (c) {
        c->baseSkill = F(self, 0x40);
        c->glueSkill = F(self, 0x44);
        c->cheatOverride = F(self, 0x54);
        c->cheat = result;
        c->cheatTime = gTime;
    }
    return result;
}

static void RedirectCall(uintptr_t site, void *target) {
    DWORD old;
    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t *)(site + 1) = (int32_t)((uintptr_t)target - (site + 5));
    VirtualProtect((void *)site, 5, old, &old);
}

static bool CallTargets(uintptr_t site, uintptr_t target) {
    const uint8_t *p = (const uint8_t *)site;
    return p[0] == 0xE8 && site + 5 + *(const int32_t *)(site + 1) == target;
}

static void Install() {
    for (GameInfo &g : kGames) {
        if (CallTargets(g.callSite, g.gripScaleFn)) {
            gGame = &g;
            break;
        }
    }
    if (!gGame) return;
    RedirectCall(gGame->callSite, (void *)&HookGripScale);

    if (gGame->simpleCallSite && CallTargets(gGame->simpleCallSite, gGame->gripScaleFn))
        RedirectCall(gGame->simpleCallSite, (void *)&HookGripScaleSimple);
    else
        gGame->simpleCallSite = 0;

    // Only take over the catch-up slot if it still holds the game's function (ExtraOptions'
    // NoCatchUp replaces it; then we leave it alone and the cheat columns stay empty).
    if (gGame->cheatSlot && *(const uintptr_t *)gGame->cheatSlot == gGame->cheatFn) {
        DWORD old;
        VirtualProtect((void *)gGame->cheatSlot, 4, PAGE_READWRITE, &old);
        *(uintptr_t *)gGame->cheatSlot = (uintptr_t)&HookCheat;
        VirtualProtect((void *)gGame->cheatSlot, 4, old, &old);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        GetModuleFileNameA(module, gDir, MAX_PATH);
        char ini[MAX_PATH];
        strcpy(ini, gDir);
        char *dot = strrchr(ini, '.');
        if (dot) strcpy(dot, ".ini");
        char *slash = strrchr(gDir, '\\');
        if (slash) slash[1] = 0;

        gVKey = GetPrivateProfileIntA("GripTelemetry", "ToggleKey", VK_F10, ini);
        Install();
    } else if (reason == DLL_PROCESS_DETACH) {
        StopRecording();
    }
    return TRUE;
}
