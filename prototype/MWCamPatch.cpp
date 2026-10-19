// MWCamPatch.asi - prototype: restore Most Wanted (2005) chase-camera "swing in" behaviour in NFS Carbon.
//
// UNTESTED. Built from static analysis of NFSC.exe v1.4 (7,217,152 bytes) vs MW speed.exe v1.3.
// Build as a 32-bit DLL and rename to .asi (needs Ultimate ASI Loader):
//   cl /LD /O2 /EHsc MWCamPatch.cpp /Fe:MWCamPatch.asi user32.lib
//   i686-w64-mingw32-g++ -shared -O2 -static -o MWCamPatch.asi MWCamPatch.cpp
//
// What it does:
//   1. Finds Carbon's CubicPovData lag tables (tTable<CubicPovData>[4 sets][5 POVs]) via the
//      lea in CubicCameraMover::Update, and restores MW's vEyeAccelMax.x values. Carbon zeroed
//      this in every table, which stops the eye from swinging toward the car on deceleration.
//   2. Optionally skips the Carbon-only brake damping block in CubicCameraMover::SetDesired
//      (eye offset.x *= 0.2 while braking above ~10 m/s).

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>

struct tTable {
    int NumEntries;
    float Min;
    float Max;
    float IndexMultiplier;
    void *Table;
};

// Layout from MW PS2 debug info (dbalatoni13/nfsmw symbols/PS2/PS2_types.nothpp), unchanged in Carbon.
struct CubicPovData {
    float fEyeDuration;
    float fLookDuration;
    float fFovDuration;
    float fUpDuration;
    float vUpAccel[4];
    float vUpAccelMin[4];
    float vUpAccelMax[4];
    float vEyeAccel[4];
    float vEyeAccelMin[4];
    float vEyeAccelMax[4];
    float vLookAccel[4];
    float vLookAccelMin[4];
    float vLookAccelMax[4];
    float vForwardDuration[4];
};
static_assert(sizeof(CubicPovData) == 0xB0, "CubicPovData size");

static const int kNumPovSets = 4;  // index comes from CameraAnchor+0x4
static const int kPovsPerSet = 5;

static uint8_t *FindPattern(const char *pattern) {
    uint8_t *base = (uint8_t *)GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER *)base;
    auto nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    size_t size = nt->OptionalHeader.SizeOfImage;

    uint8_t bytes[64];
    bool wild[64];
    int n = 0;
    for (const char *p = pattern; *p && n < 64;) {
        if (*p == ' ') { p++; continue; }
        if (*p == '?') { wild[n] = true; bytes[n++] = 0; while (*p == '?') p++; continue; }
        wild[n] = false;
        bytes[n++] = (uint8_t)strtoul(p, (char **)&p, 16);
    }
    for (size_t i = 0; i + n <= size; i++) {
        int j = 0;
        while (j < n && (wild[j] || base[i + j] == bytes[j])) j++;
        if (j == n) return base + i;
    }
    return nullptr;
}

static void RestoreEyeAccelMax() {
    // CubicCameraMover::Update: lea eax,[edi+edi*4]; add eax,ecx; lea ecx,[eax+eax*4]; lea ecx,[ecx*4+tables]
    uint8_t *p = FindPattern("8D 04 BF 03 C1 8D 0C 80 8D 0C 8D");
    if (!p) return;
    tTable *tables = *(tTable **)(p + 11);

    for (int i = 0; i < kNumPovSets * kPovsPerSet; i++) {
        tTable &t = tables[i];
        if (t.NumEntries != 2) continue;
        CubicPovData *d = (CubicPovData *)t.Table;
        if (d[0].fEyeDuration == 0.0f || d[0].vEyeAccelMax[0] != 0.0f) continue;  // bumper/hood, or already patched

        // Carbon kept MW's vEyeAccelMin.x, so use it to tell which MW POV the table came from.
        float lo, hi;
        float minX = d[0].vEyeAccelMin[0];
        if (minX == -1.25f) { lo = 1.5f; hi = 1.0f; }       // MW POV_OUTSIDE_CLOSE
        else if (minX == -2.5f) { lo = 2.0f; hi = 0.5f; }   // MW POV_SUPER_FAR / DRIFT / PURSUIT
        else if (minX == -1.0f) { lo = 2.0f; hi = 2.0f; }   // MW POV_OUTSIDE_FAR
        else continue;

        DWORD old;
        VirtualProtect(d, sizeof(CubicPovData) * 2, PAGE_READWRITE, &old);
        d[0].vEyeAccelMax[0] = lo;
        d[1].vEyeAccelMax[0] = hi;
        VirtualProtect(d, sizeof(CubicPovData) * 2, old, &old);
    }
}

static void SkipBrakeDamping() {
    // CubicCameraMover::SetDesired: je skip; cmp [edi+0xB1],bl (lookback); jne skip; ... cmp [ecx+0xE1],bl
    uint8_t *p = FindPattern("0F 84 ? ? ? ? 38 9F B1 00 00 00 0F 85 ? ? ? ? 8B 8F 90 00 00 00 38 99 E1 00 00 00");
    if (!p) return;
    // je rel32 -> nop; jmp rel32 (same target, same displacement)
    DWORD old;
    VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0x90;
    p[1] = 0xE9;
    VirtualProtect(p, 2, old, &old);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char ini[MAX_PATH];
        GetModuleFileNameA(module, ini, MAX_PATH);
        char *dot = strrchr(ini, '.');
        if (dot) strcpy(dot, ".ini");

        if (GetPrivateProfileIntA("MWCamPatch", "RestoreEyeSwing", 1, ini))
            RestoreEyeAccelMax();
        if (GetPrivateProfileIntA("MWCamPatch", "SkipBrakeDamping", 1, ini))
            SkipBrakeDamping();
    }
    return TRUE;
}
