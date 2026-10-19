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

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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

static void Install(const char *ini) {
    // Verify every site before touching anything
    for (const FloorPatch &f : kFloors) {
        uint8_t expect[8] = {0xC7, 0x44, 0x24, 0x10};
        memcpy(expect + 4, &f.stock, 4);
        if (!BytesMatch(f.addr, expect, sizeof(expect))) return;
    }
    for (const FloorPatch &f : kFloors) {
        char buf[32];
        GetPrivateProfileStringA("AIBalance", f.key, "", buf, sizeof(buf), ini);
        if (!buf[0]) continue;
        float v = strtof(buf, nullptr);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        DWORD old;
        VirtualProtect((void *)(f.addr + 4), 4, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(f.addr + 4), &v, 4);
        VirtualProtect((void *)(f.addr + 4), 4, old, &old);
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
