// AIBalance.asi - configurable AI performance floor in NFS Carbon.
//
// NFSC.exe v1.4 only (7,217,152 bytes); every patch site is byte-verified before writing.
// Build (32-bit MinGW):
//   i686-w64-mingw32-g++ -shared -O2 -static -s -o AIBalance.asi AIBalance.cpp
//
// Why: AIActionRace::ComputePotentials (0x42F4F0) plans each AI racer's top speed, cornering grip,
// acceleration and NOS as Lerp(min(AI car, player car), AI car, bias), with
// bias = Ramp(floor, player rating, 1) per axis. Carbon hardcodes the floor: 0.75 in career, 0.7
// when race-parameter query 0x63D720 is true, and 1.0 outside career (and when 0x63E180 is true,
// probably boss races). So against a weak player car the AI still plans like a 0.75-rated car.
// MW used a per-opponent GCharacter::MinimumAIPerformance instead. A floor at or below the
// player's rating gives bias 0 (the AI plans with the weaker of the two cars); 1.0 means the AI
// always plans with its own car.
//
// Patch: the three `mov dword [esp+0x10], imm32` instructions that pick the floor.
//
// RelativeMargin: in career races, replace the fixed floor with (player rating + margin) per axis, so the
// AI always plans `margin` above the player's car (up to its own car), however weak or strong that is.
// Done by redirecting ComputePotentials' three Ramp(floor, rating, 1) calls.
//
// AdaptiveMin/AdaptiveMax: clamp the career's adaptive difficulty (GRaceStatus+0xBBE8, saved as career
// stat 6000) where ComputeSkill reads it. Generic opponents (character_racy, SkillLevel 0) get all of their
// skill from it (measured: skill ~ 0.28 + 0.81 * adaptive).
//
// CatchUpCapLow/CatchUpCapHigh: the career catch-up controller (GRaceStatus::ComputeCatchUpSkill 0x643100,
// default path) caps each AI's glue skill with a curve over race progress (0/25/50/75/100%), blended
// between these two tables by (adaptive + 1) / 2. Stock {0.75, 0.75, 0.75, 0.5, 0} and
// {0.75, 0.75, 0.75, 0.75, 0.25}: catch-up (both ways) fades out over the last quarter of the race.
// CatchUpSpreadLow/CatchUpSpreadHigh: the gap (metres, at least 100) at which glue reaches full strength
// (glue = clamp(2 * gap / spread + I + D, -1, 1) * cap), same curve layout. Stock {1000, 900, 750, 750, 800} and
// {300, 275, 250, 150, 250}: at adaptive 0.6 an AI needs ~440 m of gap at the start before catch-up is at full.
// CatchUpTorque: the catch-up cheat table (stock 0.5 at every base skill). Trailing AIs get
// cheat = clamp(base + glue - 1, 0, 1) * this, and engine torque * (1 + cheat * 0.75).
//
// TorqueMatch: AI racers run simplified physics with their own (often near-maxed) cars, so they out-accelerate
// a weak player car whatever they plan. The engine's catch-up torque factor (1 + 0.75 * cheat, 0x7007D9) has no
// clamp, so in career this subtracts TorqueMatch * (1 - your car's acceleration rating) from racers' cheat there:
// the weaker your car, the less torque the AI gets. Only the engine sees it (NOS and grip use the raw cheat),
// and the wingman's override is left alone while it's active.
// TorqueMatchHold: keep the full TorqueMatch cut up to this acceleration rating, then fade it linearly to 0 at 1.0,
// so early upgrades don't take the cut away (0 = the plain TorqueMatch * (1 - rating)).
//
// Scaling with the player's car (no tiers): MarginAtWeak/MarginAtStrong and AdaptiveAtWeak/AdaptiveAtStrong
// replace RelativeMargin and the adaptive value with a blend set by your car's rating (mean of top speed,
// handling and acceleration): RatingWeak or below uses the Weak value, RatingStrong or above the Strong one.
// AdaptiveMin/AdaptiveMax still clamp the result.
//
// QuickRace=1 applies the same scaling to quick races: the margin replaces the 1.0 floor quick race uses,
// TorqueMatch applies, and AI skill becomes the career skill for your car (0.28 + 0.81 * blended adaptive)
// plus QuickRaceEasy/Medium/Hard for the chosen difficulty. ComputeSkill's quick race path reads skill from
// a 3-entry table indexed by GRaceParameters::GetDifficulty (0x40A847 / 0x40A864); both loads are pointed at
// a table refilled at each call, so Carbon's later adjustments to skill still apply.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define THISCALL __attribute__((thiscall))

typedef float (*Ramp_t)(float a, float amin, float amax);
typedef float(THISCALL *Adaptive_t)(const void *raceStatus);

static const uintptr_t kRamp = 0x401FB0;                                  // UMath::Ramp, cdecl
static const uintptr_t kRampSites[] = {0x42F5F6, 0x42F60C, 0x42F622};    // acceleration, handling, top speed
static const uintptr_t kAdaptive = 0x6127D0;                              // GRaceStatus career adaptive difficulty
static const uintptr_t kAdaptiveSite = 0x40A963;                          // in ComputeSkill
static const uintptr_t kRaceStatus = 0xA98284;                            // GRaceStatus *, context at +0x6A18 (3 = career)

static const uintptr_t kTorqueSite = 0x7007D9;    // mov eax, [ecx]; call [eax+4] (ICheater::GetCatchupCheat)
static const uintptr_t kRacerCheater = 0x9C4D40;   // AIVehicleRacecar's ICheater vtable
static const uintptr_t kVehicleVtbl = 0x9E4A78;    // PVehicle's IVehicle vtable
static const uintptr_t kPlayersBegin = 0xA9F1E4, kPlayersCount = 0xA9F1EC;

static float gMargin, gAdaptiveMin, gAdaptiveMax, gTorqueMatch, gTorqueHold;

// A value that blends with the player's car rating; off when neither end is set
struct Scaled {
    bool on;
    float weak, strong;
};

static Scaled gMarginScale, gAdaptiveScale;

static const uintptr_t kDifficulty = 0x63C5C0;  // GRaceParameters::GetDifficulty: 0 easy, 1 medium, 2 hard

// Quick race skill table loads in ComputeSkill: call GetDifficulty, then mov reg, [table + eax*4]
struct SkillSite {
    uintptr_t call, load;
    uint8_t bytes[7];
};

static const SkillSite kSkillSites[] = {
    {0x40A847, 0x40A84C, {0x8B, 0x0C, 0x85, 0x9C, 0x1B, 0x9C, 0x00}},  // catch-up off: {0.15, 0.4, 0.8}
    {0x40A864, 0x40A869, {0x8B, 0x14, 0x85, 0x90, 0x1B, 0x9C, 0x00}},  // catch-up on: {0.15, 0.4, 1}
};

static bool gQuickRace;
static float gQuickRaceOffset[3] = {-0.2f, 0.0f, 0.2f};
static float gQuickRaceSkill[3];
static float gRatingWeak = 0.3f, gRatingStrong = 0.9f;

// UMath table objects {count, min, max, 1/step, data*}
struct TablePatch {
    const char *key;
    uintptr_t table;
    int count;
    float stock[5];
};

static const TablePatch kTables[] = {
    {"CatchUpCapLow", 0xA61158, 5, {0.75f, 0.75f, 0.75f, 0.5f, 0.0f}},     // adaptive -1
    {"CatchUpCapHigh", 0xA6116C, 5, {0.75f, 0.75f, 0.75f, 0.75f, 0.25f}},  // adaptive +1
    {"CatchUpTorque", 0xA4C2AC, 3, {0.5f, 0.5f, 0.5f}},                     // by base skill; one value fills all
    {"CatchUpSpreadLow", 0xA61130, 5, {1000.0f, 900.0f, 750.0f, 750.0f, 800.0f}},  // adaptive -1
    {"CatchUpSpreadHigh", 0xA61144, 5, {300.0f, 275.0f, 250.0f, 150.0f, 250.0f}},  // adaptive +1
};

struct FloorPatch {
    const char *key;
    uintptr_t addr;     // mov dword [esp+0x10], imm32 (C7 44 24 10), imm32 at addr + 4
    float stock;
};

static const FloorPatch kFloors[] = {
    {"CareerFloor", 0x42F573, 0.75f},
    {"CareerFloorAlt", 0x42F569, 0.7f},  // career, 0x63D720 true
    {"OtherFloor", 0x42F57D, 1.0f},      // not career, or career with 0x63E180 true
};

static bool BytesMatch(uintptr_t addr, const uint8_t *bytes, size_t n) {
    return memcmp((const void *)addr, bytes, n) == 0;
}

static bool CallTargets(uintptr_t site, uintptr_t target) {
    const uint8_t *p = (const uint8_t *)site;
    return p[0] == 0xE8 && site + 5 + *(const int32_t *)(site + 1) == target;
}

static void RedirectCall(uintptr_t site, void *target) {
    DWORD old;
    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t *)(site + 1) = (int32_t)((uintptr_t)target - (site + 5));
    VirtualProtect((void *)site, 5, old, &old);
}

static bool InCareer() {
    const char *rs = *(const char *const *)kRaceStatus;
    return rs && *(const int *)(rs + 0x6A18) == 3;
}

// Quick race: context 0 with race parameters (free roam has none)
static bool InQuickRace() {
    const char *rs = *(const char *const *)kRaceStatus;
    return gQuickRace && rs && *(const int *)(rs + 0x6A18) == 0 && *(const void *const *)(rs + 0x6A1C);
}

// The floor ComputePotentials picked is one of the two career immediates (as patched), not the
// 1.0 it uses for 0x63E180 (bosses) and other modes
static bool IsCareerFloor(float floor) {
    return floor == *(const float *)(kFloors[0].addr + 4) || floor == *(const float *)(kFloors[1].addr + 4);
}

// Best player Performance per axis ([IVehicle+0xF8] TopSpeed, Handling, Acceleration; valid byte +0x104),
// as ComputePotentials takes it. False if no player has one.
static bool PlayerRating(float out[3]) {
    const char *const *players = *(const char *const *const *)kPlayersBegin;
    int count = *(const int *)kPlayersCount;
    bool found = false;
    for (int i = 0; players && i < count; i++) {
        const char *v = players[i];
        if (!v || *(const uintptr_t *)v != kVehicleVtbl || !*(const uint8_t *)(v + 0x104)) continue;
        const float *perf = (const float *)(v + 0xF8);
        for (int k = 0; k < 3; k++)
            if (!found || perf[k] > out[k]) out[k] = perf[k];
        found = true;
    }
    return found;
}

// Where the player's car sits between RatingWeak (0) and RatingStrong (1)
static float CarStrength() {
    float perf[3];
    if (!PlayerRating(perf)) return 0.0f;
    float mean = (perf[0] + perf[1] + perf[2]) / 3.0f;
    float t = (mean - gRatingWeak) / (gRatingStrong - gRatingWeak);
    return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
}

static float Blend(const Scaled &v) {
    return v.weak + (v.strong - v.weak) * CarStrength();
}

static float RelativeRamp(float floor, float rating, float top) {
    bool quick = InQuickRace() && floor == *(const float *)(kFloors[2].addr + 4);
    if ((InCareer() && IsCareerFloor(floor)) || quick) {
        floor = rating + (gMarginScale.on ? Blend(gMarginScale) : gMargin);
        if (floor > 1.0f) floor = 1.0f;
    }
    return ((Ramp_t)kRamp)(floor, rating, top);
}

typedef int(THISCALL *Difficulty_t)(const void *params);

static int THISCALL QuickRaceDifficulty(const void *params) {
    int difficulty = ((Difficulty_t)kDifficulty)(params);
    float base = 0.28f + 0.81f * Blend(gAdaptiveScale);
    for (int i = 0; i < 3; i++) {
        float v = base + gQuickRaceOffset[i];
        gQuickRaceSkill[i] = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    }
    return difficulty;
}

static float THISCALL ClampedAdaptive(const void *raceStatus) {
    float v = gAdaptiveScale.on ? Blend(gAdaptiveScale) : ((Adaptive_t)kAdaptive)(raceStatus);
    if (v < gAdaptiveMin) v = gAdaptiveMin;
    if (v > gAdaptiveMax) v = gAdaptiveMax;
    return v;
}

static float *TableData(const TablePatch &t) {
    return *(float *const *)(t.table + 16);
}

static bool TableMatches(const TablePatch &t) {
    return *(const int *)t.table == t.count && memcmp(TableData(t), t.stock, t.count * sizeof(float)) == 0;
}

// Comma-separated values; fewer than count repeats the last one. False when empty.
static bool ReadList(const char *ini, const char *key, float *out, int count) {
    char buf[128];
    GetPrivateProfileStringA("AIBalance", key, "", buf, sizeof(buf), ini);
    if (!buf[0]) return false;
    char *p = buf;
    for (int i = 0; i < count; i++) {
        char *end;
        float v = strtof(p, &end);
        if (end == p) {
            if (i == 0) return false;
            v = out[i - 1];
        } else {
            p = *end == ',' ? end + 1 : end;
        }
        out[i] = v;
    }
    return true;
}

typedef float(THISCALL *Cheat_t)(void *cheater);

static float THISCALL EngineCheat(void *cheater) {
    float cheat = ((Cheat_t)(*(void ***)cheater)[1])(cheater);
    if (*(const uintptr_t *)cheater != kRacerCheater || *(const float *)((const char *)cheater + 0x54) >= 0.0f ||
        !(InCareer() || InQuickRace()))
        return cheat;
    float perf[3];
    if (!PlayerRating(perf)) return cheat;
    float weakness = (1.0f - perf[2]) / (1.0f - gTorqueHold);
    cheat -= gTorqueMatch * (weakness > 1.0f ? 1.0f : weakness);
    return cheat < -1.0f ? -1.0f : cheat;  // torque never below 25%
}

// Optional float key: false (and *out untouched) when empty
static bool ReadFloat(const char *ini, const char *key, float *out) {
    char buf[32];
    GetPrivateProfileStringA("AIBalance", key, "", buf, sizeof(buf), ini);
    if (!buf[0]) return false;
    *out = strtof(buf, nullptr);
    return true;
}

// Weak/Strong pair; one end alone is used for both
static Scaled ReadScaled(const char *ini, const char *weakKey, const char *strongKey) {
    Scaled v = {false, 0.0f, 0.0f};
    bool weak = ReadFloat(ini, weakKey, &v.weak);
    bool strong = ReadFloat(ini, strongKey, &v.strong);
    if (weak && !strong) v.strong = v.weak;
    if (strong && !weak) v.weak = v.strong;
    v.on = weak || strong;
    return v;
}

static void Install(const char *ini) {
    // Verify every site before touching anything
    for (const FloorPatch &f : kFloors) {
        uint8_t expect[8] = {0xC7, 0x44, 0x24, 0x10};
        memcpy(expect + 4, &f.stock, 4);
        if (!BytesMatch(f.addr, expect, sizeof(expect))) return;
    }
    for (uintptr_t site : kRampSites)
        if (!CallTargets(site, kRamp)) return;
    if (!CallTargets(kAdaptiveSite, kAdaptive)) return;
    for (const TablePatch &t : kTables)
        if (!TableMatches(t)) return;
    static const uint8_t kTorqueBytes[] = {0x8B, 0x01, 0xFF, 0x50, 0x04, 0xD8, 0x0D, 0x70, 0x2D, 0x9C, 0x00};
    if (!BytesMatch(kTorqueSite, kTorqueBytes, sizeof(kTorqueBytes))) return;
    for (const SkillSite &site : kSkillSites)
        if (!CallTargets(site.call, kDifficulty) || !BytesMatch(site.load, site.bytes, sizeof(site.bytes))) return;

    for (const FloorPatch &f : kFloors) {
        float v;
        if (!ReadFloat(ini, f.key, &v)) continue;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        DWORD old;
        VirtualProtect((void *)(f.addr + 4), 4, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(f.addr + 4), &v, 4);
        VirtualProtect((void *)(f.addr + 4), 4, old, &old);
    }

    ReadFloat(ini, "RatingWeak", &gRatingWeak);
    ReadFloat(ini, "RatingStrong", &gRatingStrong);
    if (gRatingStrong <= gRatingWeak) gRatingStrong = gRatingWeak + 0.01f;

    gMarginScale = ReadScaled(ini, "MarginAtWeak", "MarginAtStrong");
    if (ReadFloat(ini, "RelativeMargin", &gMargin) || gMarginScale.on)
        for (uintptr_t site : kRampSites) RedirectCall(site, (void *)&RelativeRamp);

    gAdaptiveMin = -1.0f;
    gAdaptiveMax = 1.0f;
    bool clampMin = ReadFloat(ini, "AdaptiveMin", &gAdaptiveMin);
    bool clampMax = ReadFloat(ini, "AdaptiveMax", &gAdaptiveMax);
    gAdaptiveScale = ReadScaled(ini, "AdaptiveAtWeak", "AdaptiveAtStrong");
    if (clampMin || clampMax || gAdaptiveScale.on) RedirectCall(kAdaptiveSite, (void *)&ClampedAdaptive);

    char flag[8];
    GetPrivateProfileStringA("AIBalance", "QuickRace", "", flag, sizeof(flag), ini);
    gQuickRace = flag[0] == '1';
    ReadFloat(ini, "QuickRaceEasy", &gQuickRaceOffset[0]);
    ReadFloat(ini, "QuickRaceMedium", &gQuickRaceOffset[1]);
    ReadFloat(ini, "QuickRaceHard", &gQuickRaceOffset[2]);
    if (gQuickRace && gAdaptiveScale.on)
        for (const SkillSite &site : kSkillSites) {
            RedirectCall(site.call, (void *)&QuickRaceDifficulty);
            uint32_t table = (uint32_t)(uintptr_t)gQuickRaceSkill;
            DWORD old;
            VirtualProtect((void *)(site.load + 3), 4, PAGE_EXECUTE_READWRITE, &old);
            memcpy((void *)(site.load + 3), &table, 4);
            VirtualProtect((void *)(site.load + 3), 4, old, &old);
        }

    if (ReadFloat(ini, "TorqueMatch", &gTorqueMatch)) {
        ReadFloat(ini, "TorqueMatchHold", &gTorqueHold);
        if (gTorqueHold < 0.0f) gTorqueHold = 0.0f;
        if (gTorqueHold > 0.95f) gTorqueHold = 0.95f;
        DWORD old;
        VirtualProtect((void *)kTorqueSite, 5, PAGE_EXECUTE_READWRITE, &old);
        *(uint8_t *)kTorqueSite = 0xE8;
        VirtualProtect((void *)kTorqueSite, 5, old, &old);
        RedirectCall(kTorqueSite, (void *)&EngineCheat);
    }

    for (const TablePatch &t : kTables) {
        float v[5];
        if (!ReadList(ini, t.key, v, t.count)) continue;
        float *data = TableData(t);
        DWORD old;
        VirtualProtect(data, t.count * sizeof(float), PAGE_READWRITE, &old);
        memcpy(data, v, t.count * sizeof(float));
        VirtualProtect(data, t.count * sizeof(float), old, &old);
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
