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
    // and the ICheater::GetCatchupCheat vtable slots (AIPerpVehicle, and AIVehicleRacecar which
    // racers use) + the original function both slots hold.
    uintptr_t simpleCallSite;
    uintptr_t cheatSlots[2], cheatFn;
    // AIActionRace::ComputePotentials (performance bias, planned limits) and its two call sites,
    // PVehicle's IVehicle vtable (to validate pointers before reading the cached Performance),
    // the player IVehicle list (begin pointer, count), the GRaceStatus pointer and the two race
    // parameter queries ComputePotentials uses to pick its floor.
    uintptr_t potentialsFn, potentialsSites[2];
    uintptr_t vehicleVtbl, playersBegin, playersCount;
    uintptr_t raceStatus, raceFlagA, raceFlagB;
    // PVehicle factory: the constructor call (its 8th argument is the Performance the car is rated
    // with, VehicleParams+0x34 or an estimate) and the call to the estimator it falls back on.
    uintptr_t vehicleCtorSite, vehicleCtor, estimateSite, estimateFn;
};

// Offsets from MW decomp (SuspensionRacer, PC layout = console + 8) and matching Carbon disassembly
static GameInfo kGames[] = {
    {"MW", 0x6B1F79, 0x68D5F0, 0x8F8AA8, 0x1B4,
     {0xD4, 0xD8, 0xDC, 0x110, 0x118, 0x128, 0xEC, 0x12C, 0xFC, 0xD0, 0xF0, 0xE0, 0xC4, -1, -1, -1},
     {0x11C, 0x120, 0x124, 0x12C, 0x1B0, 0x104, 0x108, 0x10C, 0x114, 0xA0, 0x4C, -1, -1, -1, -1}, 0, {0, 0}, 0},
    {"Carbon", 0x703C05, 0x6DE890, 0xA6358C, 0x1C0,
     {0xD0, 0xD4, 0xD8, 0x10C, 0x114, 0x11C, 0xE8, 0x120, 0xF8, 0xCC, 0xEC, 0xDC, 0xC0, 0x124, 0x128, 0x12C},
     {0x104, 0x108, 0x10C, 0x114, 0x198, 0xEC, 0xF0, 0xF4, 0xFC, 0x90, 0x88, 0xB0, 0x7C, 0x6C, 0x54}, 0x7046E8, {0x9C4ABC, 0x9C4D44}, 0x40AE80,
     0x42F4F0, {0x436005, 0x4361E9}, 0x9E4A78, 0xA9F1E4, 0xA9F1EC, 0xA98284, 0x63D720, 0x63E180,
     0x6DAE34, 0x6DA530, 0x6DADA7, 0x6CE330},
};

// AIActionRace fields written by ComputePotentials (Carbon): bias TopSpeed/Handling/Acceleration,
// planned top speed (m/s), start/end lateral grip (g), NOS capability, usable NOS, then the aivehicle
// TopSpeedMultiplier, MAXIMUM_AI_SPEED (m/s) and AccelerationMultiplier cached by the action's setup.
static const int kPlanOffsets[] = {0x7C, 0x80, 0x84, 0xA4, 0x9C, 0xA0, 0x88, 0x90, 0xC0, 0xC4, 0xC8};
static const int kNumPlan = sizeof(kPlanOffsets) / sizeof(kPlanOffsets[0]);

// Per-car state for the race log, keyed by the car's COM object (shared by all its interfaces:
// [ISimable+4] from the suspension's owner, [ICheater+4] from the catch-up hook).
struct CarRec {
    const void *key;
    int id;
    double dist, lastLog, cheatTime;
    float baseSkill, glueSkill, cheat, cheatOverride;
    float perf[3];          // the car's own Physics::Info::Performance (TopSpeed, Handling, Acceleration)
    float plan[kNumPlan];   // AI only, see kPlanOffsets
    DWORD seen;             // GetTickCount of the last lookup
};
static CarRec gCars[64];
static int gNumCars = 0;
static FILE *gRaceFile = nullptr;
static double gPlayerDist = 0.0;

static CarRec *FindCar(const void *key) {
    DWORD now = GetTickCount();
    for (int i = 0; i < gNumCars; i++)
        if (gCars[i].key == key) {
            gCars[i].seen = now;
            return &gCars[i];
        }
    // Table full (cars from earlier races are never removed): reuse the one seen longest ago
    int slot = gNumCars;
    if (gNumCars < 64) gNumCars++;
    else
        for (int i = slot = 0; i < 64; i++)
            if (now - gCars[i].seen > now - gCars[slot].seen) slot = i;
    CarRec &c = gCars[slot];
    c = CarRec{key, slot, 0.0, -1.0, -1.0, NAN, NAN, NAN, NAN};
    for (float &v : c.perf) v = NAN;
    for (float &v : c.plan) v = NAN;
    c.seen = now;
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
                               "catchup_cheat,cheat_override,cheat_age,perf_top,perf_handling,perf_accel,bias_top,"
                               "bias_handling,bias_accel,plan_top_mps,plan_grip_start,plan_grip_end,nos_capability,"
                               "usable_nos,topspeed_mult,max_ai_speed_mps,accel_mult,race_active,adaptive\n");
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

// Empty column instead of NAN (msvcrt prints NAN as 1.#QNAN)
static void PutOptional(float v) {
    if (std::isnan(v)) fputc(',', gRaceFile);
    else fprintf(gRaceFile, ",%.4f", v);
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
    fprintf(gRaceFile, "%.3f,%d,%d,%d,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%d,%.3f,%.3f,%.4f,%.4f,%.2f", gTime, c->id, cls, physics,
            F(s, 0xBC) * 2.23694f, c->dist, c->dist - gPlayerDist, F(s, 0x30), F(s, 0x34), F(s, 0x38), F(s, 0x4C), F(s, 0x5C),
            *(const short *)(s + 0xCC), c->baseSkill, c->glueSkill, c->cheat, c->cheatOverride,
            c->cheatTime < 0 ? -1.0 : gTime - c->cheatTime);
    for (float v : c->perf) PutOptional(v);
    for (float v : c->plan) PutOptional(v);
    // Race parameters go away when the player finishes (gaps after that are meaningless); career
    // adaptive difficulty (GRaceStatus+0xBBE8), the source of generic opponents' skill
    const char *rs = *(const char *const *)gGame->raceStatus;
    fprintf(gRaceFile, ",%d", rs && *(const void *const *)(rs + 0x6A1C) ? 1 : 0);
    PutOptional(rs ? F(rs, 0xBBE8) : NAN);
    fputc('\n', gRaceFile);
}

// Copies the Performance a PVehicle was created with ([IVehicle+0xF8], valid flag at +0x104).
// Returns false for anything that isn't a PVehicle's IVehicle or has no rating.
static bool ReadPerf(const void *vehicle, float out[3]) {
    const char *v = (const char *)vehicle;
    if (!v || *(const uintptr_t *)v != gGame->vehicleVtbl || !*(const uint8_t *)(v + 0x104)) return false;
    memcpy(out, v + 0xF8, 3 * sizeof(float));
    return true;
}

typedef void(THISCALL *Potentials_t)(void *self);
typedef bool(THISCALL *RaceFlag_t)(const void *params);

static FILE *gPotFile = nullptr;
static DWORD gStartTick;

// One line per ComputePotentials call in potentials_<game>.csv (always on, appended): the race
// state that picks the floor ([GRaceStatus+0x6A18] context, [+0x6A1C] parameters, the two flag
// queries) and the bias it produced, to explain calls that come back with bias 0.
static void LogPotentials(const void *self, const CarRec *c, uintptr_t site, const float best[3], bool haveBest) {
    if (!gPotFile) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%spotentials_%s.csv", gDir, gGame->name);
        gPotFile = fopen(path, "a");
        if (!gPotFile) return;
        fseek(gPotFile, 0, SEEK_END);
        if (ftell(gPotFile) == 0)
            fprintf(gPotFile, "wall_s,rec_t,site,car,race_status,context,params,flag_63D720,flag_63E180,best_top,"
                              "best_handling,best_accel,bias_top,bias_handling,bias_accel,plan_top_mps\n");
    }
    const char *rs = *(const char *const *)gGame->raceStatus;
    const void *params = rs ? *(const void *const *)(rs + 0x6A1C) : nullptr;
    fprintf(gPotFile, "%.2f,", (GetTickCount() - gStartTick) / 1000.0);
    if (gFile) fprintf(gPotFile, "%.3f", gTime);
    fprintf(gPotFile, ",%s,%d,%d,%d,%d,%d,%d", site == gGame->potentialsSites[0] ? "construct" : "start", c ? c->id : -1,
            rs != nullptr, rs ? I(rs, 0x6A18) : -1, params != nullptr,
            params ? ((RaceFlag_t)gGame->raceFlagA)(params) : -1, params ? ((RaceFlag_t)gGame->raceFlagB)(params) : -1);
    for (int i = 0; i < 3; i++) {
        if (haveBest) fprintf(gPotFile, ",%.4f", best[i]);
        else fputc(',', gPotFile);
    }
    fprintf(gPotFile, ",%.4f,%.4f,%.4f,%.4f\n", F(self, 0x7C), F(self, 0x80), F(self, 0x84), F(self, 0xA4));
    fflush(gPotFile);
}

// AIActionRace::ComputePotentials runs when a race action starts. Records what the AI planned with
// and the ratings it was computed from (its own car via [this+0x34] = IVehicle, and every player's).
static void THISCALL HookPotentials(void *self) {
    uintptr_t site = (uintptr_t)__builtin_return_address(0) - 5;
    ((Potentials_t)gGame->potentialsFn)(self);
    const char *vehicle = *(const char *const *)((const char *)self + 0x34);
    CarRec *c = vehicle ? FindCar(*(const void *const *)(vehicle + 4)) : nullptr;
    if (c) {
        ReadPerf(vehicle, c->perf);
        for (int i = 0; i < kNumPlan; i++) c->plan[i] = F(self, kPlanOffsets[i]);
    }
    // Best (highest) player rating per axis, as ComputePotentials' own lookup (0x420F00) takes it
    float best[3] = {0.0f, 0.0f, 0.0f};
    bool haveBest = false;
    const char *const *players = *(const char *const *const *)gGame->playersBegin;
    int count = *(const int *)gGame->playersCount;
    for (int i = 0; players && i < count; i++) {
        if (!players[i]) continue;
        float perf[3];
        if (!ReadPerf(players[i], perf)) continue;
        for (int k = 0; k < 3; k++) best[k] = haveBest ? fmaxf(best[k], perf[k]) : perf[k];
        haveBest = true;
        if (CarRec *p = FindCar(*(const void *const *)(players[i] + 4))) memcpy(p->perf, perf, sizeof(perf));
    }
    LogPotentials(self, c, site, best, haveBest);
}

typedef bool (*Estimate_t)(const void *vehicle, float *out);
typedef void *(THISCALL *VehicleCtor_t)(void *self, uintptr_t cls, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5,
                                        uintptr_t a6, uintptr_t a7, const float *matched, uintptr_t a9);

static FILE *gVehFile = nullptr;
static const float *gLastEstimate = nullptr;
static uintptr_t gCodeLo, gCodeHi;

// Physics::Info estimate the factory uses when VehicleParams carries no rating (flag 0x8)
static bool HookEstimate(const void *vehicle, float *out) {
    bool ok = ((Estimate_t)gGame->estimateFn)(vehicle, out);
    gLastEstimate = ok ? out : nullptr;
    return ok;
}

// One line per PVehicle in vehicles_<game>.csv (always on, appended): driver class, where its
// Performance rating came from (explicit = VehicleParams+0x34 from the creator, estimated, none),
// the rating, and the return addresses of direct calls found on the stack (who created it).
static void *THISCALL HookVehicleCtor(void *self, uintptr_t cls, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5,
                                      uintptr_t a6, uintptr_t a7, const float *matched, uintptr_t a9) {
    const char *source = !matched ? "none" : matched == gLastEstimate ? "estimated" : "explicit";
    float perf[3] = {NAN, NAN, NAN};
    if (matched) memcpy(perf, matched, sizeof(perf));
    gLastEstimate = nullptr;
    void *result = ((VehicleCtor_t)gGame->vehicleCtor)(self, cls, a2, a3, a4, a5, a6, a7, matched, a9);

    if (!gVehFile) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%svehicles_%s.csv", gDir, gGame->name);
        gVehFile = fopen(path, "a");
        if (!gVehFile) return result;
        fseek(gVehFile, 0, SEEK_END);
        if (ftell(gVehFile) == 0) fprintf(gVehFile, "wall_s,class,key,source,perf_top,perf_handling,perf_accel,callers\n");
    }
    const char *vehicle = (const char *)self + 0xB8;  // IVehicle subobject
    fprintf(gVehFile, "%.2f,%u,%p,%s", (GetTickCount() - gStartTick) / 1000.0, (unsigned)cls, *(void *const *)(vehicle + 4), source);
    for (float v : perf) {
        if (std::isnan(v)) fputc(',', gVehFile);
        else fprintf(gVehFile, ",%.4f", v);
    }
    fputc(',', gVehFile);
    const uintptr_t *sp = (const uintptr_t *)__builtin_frame_address(0);
    const uintptr_t *top = (const uintptr_t *)((const NT_TIB *)NtCurrentTeb())->StackBase;
    for (int found = 0; sp < top && found < 8; sp++) {
        uintptr_t v = *sp;
        if (v < gCodeLo + 5 || v >= gCodeHi || *(const uint8_t *)(v - 5) != 0xE8) continue;
        fprintf(gVehFile, "%s%X", found++ ? " " : "", (unsigned)v);
    }
    fputc('\n', gVehFile);
    fflush(gVehFile);
    return result;
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
    for (uintptr_t slot : gGame->cheatSlots)
        if (slot && *(const uintptr_t *)slot == gGame->cheatFn) {
            DWORD old;
            VirtualProtect((void *)slot, 4, PAGE_READWRITE, &old);
            *(uintptr_t *)slot = (uintptr_t)&HookCheat;
            VirtualProtect((void *)slot, 4, old, &old);
        }

    if (gGame->potentialsFn)
        for (uintptr_t site : gGame->potentialsSites)
            if (CallTargets(site, gGame->potentialsFn)) RedirectCall(site, (void *)&HookPotentials);

    if (gGame->vehicleCtorSite && CallTargets(gGame->vehicleCtorSite, gGame->vehicleCtor) &&
        CallTargets(gGame->estimateSite, gGame->estimateFn)) {
        const char *base = (const char *)GetModuleHandleA(nullptr);
        const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
        gCodeLo = (uintptr_t)base + nt->OptionalHeader.BaseOfCode;
        gCodeHi = gCodeLo + nt->OptionalHeader.SizeOfCode;
        RedirectCall(gGame->estimateSite, (void *)&HookEstimate);
        RedirectCall(gGame->vehicleCtorSite, (void *)&HookVehicleCtor);
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
        gStartTick = GetTickCount();
        Install();
    } else if (reason == DLL_PROCESS_DETACH) {
        StopRecording();
        if (gPotFile) fclose(gPotFile);
        if (gVehFile) fclose(gVehFile);
    }
    return TRUE;
}
