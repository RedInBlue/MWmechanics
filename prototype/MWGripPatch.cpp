// MWGripPatch.asi - restore Most Wanted (2005) tire grip behaviour in NFS Carbon.
//
// NFSC.exe v1.4 only (7,217,152 bytes); every patch site is byte-verified before writing.
// Build (32-bit MinGW):
//   i686-w64-mingw32-g++ -shared -O2 -static -s -masm=intel -o MWGripPatch.asi MWGripPatch.cpp
//
// See MW_Grip_Notes.md for the analysis behind each switch.
//
// RestoreGripScale (default on)
//   Chassis::ComputeLateralGripScale @ 0x6DE890 computes MW's GripVsSpeed(speed) * 1.2, then
//   collapses it to 1.0f/0.0f. The result becomes every tire's mGripBoost (cornering force scale),
//   so Carbon runs at 1.0 where MW runs ~1.0-1.24. Patch: return the computed value.
//
// RestoreThrottleEllipse (default off)
//   MW's Tire::UpdateLoaded counts longitudinal force x1.5 against the grip limit while drive
//   torque pushes in the direction of travel (kept x1.5 when saturated, undone otherwise).
//   Carbon removed it. Patch: two hooks around Carbon's force-limit block.
//
// RemoveBurnoutHack (default off)
//   Carbon-only special case in Tire::UpdateLoaded (max force x0.2, slip x10, long force x6
//   under a set of throttle/brake/gear conditions). Patch: always skip it.

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>

extern "C" {
uint8_t gEllipseActive = 0;
float kEllipseRatio = 1.5f;  // MW TireForceEllipseRatio
void EllipseBegin();
void EllipseEnd();
}

// Replaces "fld [esi+0xD8]" at 0x6EF032 (esi = tire, start of the |force| > max_force check).
// if (!mBrakeLocked && (mDriveTorque + mBrakeTorque) * mRoadSpeed > 0) mLongitudeForce *= 1.5
asm(R"(
    .globl _EllipseBegin
_EllipseBegin:
    mov byte ptr [_gEllipseActive], 0
    cmp byte ptr [esi+0x134], 0
    jne 1f
    fld dword ptr [esi+0xDC]
    fadd dword ptr [esi+0xE0]
    fmul dword ptr [esi+0xF8]
    fldz
    fcomip st, st(1)
    fstp st(0)
    jae 1f
    fld dword ptr [esi+0xD8]
    fmul dword ptr [_kEllipseRatio]
    fstp dword ptr [esi+0xD8]
    mov byte ptr [_gEllipseActive], 1
1:
    fld dword ptr [esi+0xD8]
    ret
)");

// Replaces "fld [esp+0x3C]; pop ebx" at 0x6EF0E9 (both limit branches meet here).
// If the ellipse was applied and the tire did not saturate (mTraction == 1), undo the x1.5.
asm(R"(
    .globl _EllipseEnd
_EllipseEnd:
    cmp byte ptr [_gEllipseActive], 0
    je 1f
    mov byte ptr [_gEllipseActive], 0
    fld dword ptr [esi+0x10C]
    fld1
    fcomip st, st(1)
    fstp st(0)
    jne 1f
    fld dword ptr [esi+0xD8]
    fdiv dword ptr [_kEllipseRatio]
    fstp dword ptr [esi+0xD8]
1:
    fld dword ptr [esp+0x40]
    mov ebx, [esp+4]
    pop eax
    add esp, 4
    jmp eax
)");

static bool BytesMatch(uintptr_t addr, const char *hex) {
    const uint8_t *p = (const uint8_t *)addr;
    char *end;
    for (const char *s = hex; *s; s = end) {
        if (*p++ != (uint8_t)strtoul(s, &end, 16)) return false;
        if (end == s) break;
    }
    return true;
}

static void Write(uintptr_t addr, const void *data, size_t len) {
    DWORD old;
    VirtualProtect((void *)addr, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)addr, data, len);
    VirtualProtect((void *)addr, len, old, &old);
}

static void WriteCall(uintptr_t site, void *target, size_t len) {
    uint8_t buf[8] = {0xE8};
    *(int32_t *)(buf + 1) = (int32_t)((uintptr_t)target - (site + 5));
    for (size_t i = 5; i < len; i++) buf[i] = 0x90;
    Write(site, buf, len);
}

static void RestoreGripScale() {
    // ...fmul [1.2]; fiadd [esp+8]; ... -> ...fmul [1.2]; pop ecx; ret 4
    if (!BytesMatch(0x6DE8EC, "D8 0D 40 4F 9E 00 DA 44 24 08")) return;
    static const uint8_t ret[] = {0x59, 0xC2, 0x04, 0x00};
    Write(0x6DE8F2, ret, sizeof(ret));
}

static void RestoreThrottleEllipse() {
    if (!BytesMatch(0x6EF032, "D9 86 D8 00 00 00") || !BytesMatch(0x6EF0E9, "D9 44 24 3C 5B")) return;
    WriteCall(0x6EF032, (void *)&EllipseBegin, 6);
    WriteCall(0x6EF0E9, (void *)&EllipseEnd, 5);
}

static void RestoreBurnoutTraction() {
    // SuspensionRacer::DoDriveForces, burnout/fishtail branch. MW: spinning wheel traction boost = T
    // (BurnoutFriction table, 0.65-1.0), other driven wheel = 1.0. Carbon: 2T and 2.0, which kills
    // launch wheelspin. Patch the 2.0 constants to 1.0 and NOP the "fadd st,st" doublings.
    if (!BytesMatch(0x6E0783, "C7 44 24 28 00 00 00 40") || !BytesMatch(0x6E0795, "DC C0") ||
        !BytesMatch(0x6E07B5, "C7 44 24 2C 00 00 00 40") || !BytesMatch(0x6E07CD, "DC C0"))
        return;
    static const uint8_t one[] = {0x00, 0x00, 0x80, 0x3F};
    static const uint8_t nop2[] = {0x90, 0x90};
    Write(0x6E0787, one, 4);
    Write(0x6E0795, nop2, 2);
    Write(0x6E07B9, one, 4);
    Write(0x6E07CD, nop2, 2);
}

static void RemovePitchRollScaling() {
    // SuspensionRacer::DoWheelForces. MW only rescales yaw torque (x YAW_SPEED). Carbon also rescales
    // pitch (chassis +0x6C / +0x7C by sign) and roll (chassis +0x54). NOP those three fmuls so pitch
    // and roll torque pass through unchanged, like MW.
    if (!BytesMatch(0x6FB162, "75 05 D8 48 6C EB 03 D8 48 7C") || !BytesMatch(0x6FB1B6, "D8 48 54")) return;
    static const uint8_t nop3[] = {0x90, 0x90, 0x90};
    Write(0x6FB164, nop3, 3);
    Write(0x6FB169, nop3, 3);
    Write(0x6FB1B6, nop3, 3);
}

static void RemoveBurnoutHack() {
    // jp 0x6EF00E -> jmp 0x6EF00E (always take the "not burnout" path)
    if (!BytesMatch(0x6EEFCA, "7A 42")) return;
    static const uint8_t jmp = 0xEB;
    Write(0x6EEFCA, &jmp, 1);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char ini[MAX_PATH];
        GetModuleFileNameA(module, ini, MAX_PATH);
        char *dot = strrchr(ini, '.');
        if (dot) strcpy(dot, ".ini");

        if (GetPrivateProfileIntA("MWGripPatch", "RestoreGripScale", 1, ini)) RestoreGripScale();
        if (GetPrivateProfileIntA("MWGripPatch", "RestoreThrottleEllipse", 0, ini)) RestoreThrottleEllipse();
        if (GetPrivateProfileIntA("MWGripPatch", "RemoveBurnoutHack", 0, ini)) RemoveBurnoutHack();
        if (GetPrivateProfileIntA("MWGripPatch", "RestoreBurnoutTraction", 1, ini)) RestoreBurnoutTraction();
        if (GetPrivateProfileIntA("MWGripPatch", "RemovePitchRollScaling", 0, ini)) RemovePitchRollScaling();
    }
    return TRUE;
}
