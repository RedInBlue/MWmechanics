// MWTachPatch.asi - restore Most Wanted (2005) style tachometer needle behaviour in NFS Carbon.
//
// NFSC.exe v1.4 only (7,217,152 bytes); every patch site is byte-verified before writing.
// Build (32-bit MinGW):
//   i686-w64-mingw32-g++ -shared -O2 -static -s -o MWTachPatch.asi MWTachPatch.cpp
//
// Why: MW's LocalPlayer::UpdateHud reads IEngine::GetRPM(), then overrides it with
// ICarAudio::GetRPM() (SoundCar::mSoundRPM, the audio engine's "audible" RPM, which glides
// through gear changes) before calling ITachometer::SetRpm. Carbon still computes
// mSoundRPM (SoundCar::OnService @ 0x6E54A0) and its HUD RPM getter even still queries
// ICarAudio, but discards the result, so the needle follows the raw physics RPM and snaps on shifts.
//
// Fix: redirect the HUD data-layer RPM getter (DAL id 2002, called at 0x4CC064) to a wrapper
// that runs the original and then substitutes ICarAudio::GetRPM() when it's available.

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#define THISCALL __attribute__((thiscall))

typedef bool(THISCALL *GetRpm_t)(void *self, float *out, int player);
typedef void *(THISCALL *GetSimable_t)(void *self, int player);
typedef void *(THISCALL *QueryInterface_t)(void *com, void *handle);
typedef void *(THISCALL *GetIface_t)(void *obj);
typedef float(THISCALL *GetFloat_t)(void *obj);

static const uintptr_t kCallSite = 0x4CC064;          // call DALVehicle::GetRpm inside the DAL float switch (case 2002)
static const uintptr_t kGetRpm = 0x4B65F0;            // original: *out = IEngine::GetRPM(); then dead ICarAudio query
static const uintptr_t kGetSimable = 0x4B62B0;        // DAL helper: player index -> simable
static const uintptr_t kQueryInterface = 0x60CB50;    // UTL::COM::Object::_mInterfaces.Find(handle)
static const uintptr_t kICarAudioHandle = 0x4A9D10;   // ICarAudio::_IHandle

static GetRpm_t OrigGetRpm = (GetRpm_t)kGetRpm;

// Redline bounce timing. SFXCTL_Engine::UpdateRPM (0x510DF0) moves the redlining visual offset
// 0 -> 200 -> 0 with smooth(cur, target, 50), a fixed step per update (8 updates per cycle).
// MW ran that at 60 Hz (~133 ms per cycle); with WidescreenFix SimRate it runs at 2x refresh, so
// the needle buzzes. This stub replaces both smooth() calls and scales the step by dt * 60.
// Stack at entry: [esp+4] cur, [esp+8] target, [esp+0xC] step, [esp+0x24] UpdateRPM's dt.
extern "C" {
float kRefRate = 60.0f;
float kMaxScale = 4.0f;
void RedlineSmooth();
}

asm(R"(
    .intel_syntax noprefix
    .globl _RedlineSmooth
_RedlineSmooth:
    fld dword ptr [esp+0x24]
    fmul dword ptr [_kRefRate]
    fldz
    fcomip st, st(1)
    jae 1f
    fld dword ptr [_kMaxScale]
    fcomip st, st(1)
    jb 1f
    fmul dword ptr [esp+0xC]
    fstp dword ptr [esp+0xC]
    jmp 2f
1:
    fstp st(0)
2:
    mov eax, 0x50AA90
    jmp eax
    .att_syntax prefix
)");

static bool THISCALL HookGetRpm(void *self, float *out, int player) {
    bool ok = OrigGetRpm(self, out, player);
    if (!ok || !out)
        return ok;

    // Same lookup chain the original getter uses: simable -> COM object -> ICarAudio
    void *simable = ((GetSimable_t)kGetSimable)(self, player);
    if (!simable)
        return ok;
    void *iface = ((GetIface_t)(*(void ***)simable)[1])(simable);
    if (!iface)
        return ok;
    void *carAudio = ((QueryInterface_t)kQueryInterface)(*(void **)((char *)iface + 4), (void *)kICarAudioHandle);
    if (!carAudio)
        return ok;

    float audioRpm = ((GetFloat_t)(*(void ***)carAudio)[1])(carAudio);  // SoundCar::GetRPM -> mSoundRPM
    if (audioRpm > 0.0f)
        *out = audioRpm;
    return ok;
}

static bool BytesMatch(uintptr_t addr, const char *hex) {
    const uint8_t *p = (const uint8_t *)addr;
    char *end;
    for (const char *s = hex; *s; s = end) {
        if (*p++ != (uint8_t)strtoul(s, &end, 16)) return false;
        if (end == s) break;
    }
    return true;
}

static void Install() {
    // Verify this is the exe we analysed before touching anything
    if (!BytesMatch(kCallSite, "E8 87 A5 FE FF")) return;
    if (!BytesMatch(kGetRpm, "56 57 8B 7C 24 10 57 8B F1 E8 B2 FC FF FF")) return;
    if (!BytesMatch(0x4B6674, "68 10 9D 4A 00 E8 D2 64 15 00")) return;

    DWORD old;
    VirtualProtect((void *)kCallSite, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t *)(kCallSite + 1) = (int32_t)((uintptr_t)&HookGetRpm - (kCallSite + 5));
    VirtualProtect((void *)kCallSite, 5, old, &old);
}

// MW's needle shakes at redline because SFXCTL_Engine::UpdateRPM adds a 0..200 "redlining visual
// offset" bounce once the audio RPM (1000-10000 scale) passes 9800. Carbon kept that code, but
// SoundCar::OnService (0x6E54A0) maps physics RPM to audio RPM over idle..MAX_RPM for cars without
// the rev limiter, so it never saturates at redline. MW always used idle..RED_LINE.
// Patch: je -> jmp at 0x6E5510 so the RED_LINE range is always used.
static void InstallRedlineBounce() {
    if (!BytesMatch(0x6E5510, "74 13 8B 11 FF 52 4C")) return;
    DWORD old;
    VirtualProtect((void *)0x6E5510, 1, PAGE_EXECUTE_READWRITE, &old);
    *(uint8_t *)0x6E5510 = 0xEB;
    VirtualProtect((void *)0x6E5510, 1, old, &old);
}

static bool CallsTo(uintptr_t site, uintptr_t target) {
    return *(const uint8_t *)site == 0xE8 && site + 5 + *(const int32_t *)(site + 1) == target;
}

static void InstallSmoothShake() {
    const uintptr_t sites[2] = {0x510F3E, 0x510F7D};  // smooth() toward 200 / back to 0
    if (!BytesMatch(0x510F2B, "68 00 00 48 42")) return;  // push 50.0f
    for (uintptr_t s : sites)
        if (!CallsTo(s, 0x50AA90)) return;
    for (uintptr_t s : sites) {
        DWORD old;
        VirtualProtect((void *)s, 5, PAGE_EXECUTE_READWRITE, &old);
        *(int32_t *)(s + 1) = (int32_t)((uintptr_t)&RedlineSmooth - (s + 5));
        VirtualProtect((void *)s, 5, old, &old);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char ini[MAX_PATH];
        GetModuleFileNameA(module, ini, MAX_PATH);
        char *dot = strrchr(ini, '.');
        if (dot) strcpy(dot, ".ini");

        if (GetPrivateProfileIntA("MWTachPatch", "UseAudioRPM", 1, ini))
            Install();
        if (GetPrivateProfileIntA("MWTachPatch", "RedlineBounce", 0, ini))
            InstallRedlineBounce();
        if (GetPrivateProfileIntA("MWTachPatch", "SmoothRedlineShake", 1, ini))
            InstallSmoothShake();
    }
    return TRUE;
}
