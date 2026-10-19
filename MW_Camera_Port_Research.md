# Porting NFS Most Wanted (2005) camera behaviour to NFS Carbon — research notes

Binaries analysed (static disassembly only, nothing run in-game):

- **Carbon**: `NFSC.exe` v1.4, 7,217,152 bytes (`Desktop\Files\Game Files\Need for Speed Carbon BR`)
- **MW**: `speed.exe` v1.3, 6,029,312 bytes (from the *Pepega Mod* folder. Values match the PS2 debug-symbol layout, but confirm against a clean v1.3 exe if possible)
- **Reference**: MW PS2 debug info from the [dbalatoni13/nfsmw](https://github.com/dbalatoni13/nfsmw) decompilation (`symbols/PS2/*`), which provides class layouts, function names, locals and globals for the MW camera.

## TL;DR

1. **Why porting camerainfo VLT values isn't enough.** The values in `camerainfo` (ANGLE/FOV/HEIGHT/LAG/LATOFFSET/STIFFNESS/TILTING) only set the camera's *rest* position. The camera's *motion* comes from `CubicCameraMover`, which is driven by hard-coded tables in the exe's `.data`, not by VLT:
   - `aCubicPovTables`: per-POV `CubicPovData` (eye, look, up and FOV durations, plus the accel-lag scale, min and max)
   - `CameraAccelerationCurve`, `CameraSpeedHugData`, `CameraGearChangingCurve`, `CameraImpcatCurveH/V`
2. **Carbon still runs MW's camera code.** It's the same `CubicCameraMover` (Update, SetDesired, CameraAccelCurve, CameraSpeedHug), and its tables are copied from MW almost verbatim.
3. **Carbon changed three things that kill the MW feel:**
   - **`vEyeAccelMax.x = 0` in every `CubicPovData` table.** MW uses 1.0–2.0. The eye offset is `accelCurve(accel) * vEyeAccel` (with `vEyeAccel.x = -1`), clamped to `[vEyeAccelMin, vEyeAccelMax]`. Accelerating gives a negative x, so the camera pulls back; Carbon still allows this. Decelerating or braking gives a positive x, so the camera **swings in toward the car**; Carbon clamps that to 0.
   - **A new brake-damping block in `SetDesired`** (0x491E02). When braking above about 10 m/s it scales the eye offset x by 0.2 and adjusts another term. MW has no equivalent.
   - **Tables became 2D**: `tables[anchor->set * 5 + povType]` (4 sets × 5 POVs), and `CameraAccelCurve` gained the same set index. MW used `tables[povType]` with 7 POVs. All four accel-curve sets still contain MW's curve.
4. **The cheapest test is a data patch.** Write MW's `vEyeAccelMax.x` back into Carbon's 12 non-bumper/hood tables (24 floats), and optionally jump over the brake block. A prototype is at [prototype/MWCamPatch.cpp](prototype/MWCamPatch.cpp).
5. **Carbon's `Update` is still about 40% larger** than MW's (0xE10 vs 0xA70 bytes) and makes many extra calls. If the data patch doesn't fully restore the feel, the remaining difference is in that extra logic, and a full port means hooking `CubicCameraMover::Update`/`SetDesired` and reimplementing MW's versions.

## Address map

| Item | MW v1.3 (`speed.exe`) | Carbon v1.4 (`NFSC.exe`) | MW PS2 (symbols) |
|---|---|---|---|
| `CubicCameraMover::CameraAccelCurve` | 0x47CDD0 | 0x491910 (takes extra `set` arg) | 0x1728C8 |
| `CubicCameraMover::CameraSpeedHug` | 0x47CE80 | 0x4919D0 | 0x172980 |
| `CubicCameraMover::SetDesired` | 0x47CF00 | 0x491A50 | 0x172A28 |
| `CubicCameraMover::Update` | 0x47D3C0 (size ~0xA70) | 0x4921B0 (size ~0xE10) | 0x172F20 (0xA2C) |
| `aCubicPovTables` (`tTable<CubicPovData>`) | 0x8EE040 `[7]` | 0xA579B8 `[4][5]` | 0x5E0900 |
| `CameraAccelerationCurve` | 0x8EDE54 `[5]` (range ±30) | 0xA4F6B0 `[4][5]`, ranges at 0x9C7128 | 0x5E0A38 |
| `CameraSpeedHugData` (`bVector2[5]`) | 0x8EE0CC | 0xA57990 (1.13 vs MW 1.125, otherwise identical) | 0x5E0A10 |
| `CameraAnchor*` in mover | `+0x80` | `+0x90` | `+0x90` |
| Lookback flag / POV used | — | `+0xB1` / `+0xAC` (from ThirteenAG WSF) | `+0xB4` / `+0xAC` |

`CubicPovData` is 0xB0 bytes: 4 durations, then `bVector4`s for UpAccel/Min/Max, EyeAccel/Min/Max, LookAccel/Min/Max and ForwardDuration. `vEyeAccelMax` is at +0x60. The layout is identical in both games.

### Lag table diff (entry [0] = low speed, [1] = high speed; the speed blend is `velMag * 0.01408`, clamped to 0..1, in both games)

| MW POV | MW EyeAccelMax.x | Carbon equivalent | Carbon EyeAccelMax.x |
|---|---|---|---|
| Close (Min.x -1.25) | 1.5 / 1.0 | set0/1/3 POV2 | **0 / 0** |
| Far (Min.x -1.0) | 2.0 / 2.0 | every set's POV3 and POV4 | **0 / 0** |
| SuperFar/Drift/Pursuit (Min.x -2.5) | 2.0 / 0.5 | set2 POV2 | **0 / 0** |

Every other field (durations, UpAccel, EyeAccel, LookAccel, all mins, ForwardDuration) matches MW exactly.

Carbon v1.4 addresses of `vEyeAccelMax.x` ([0], [1]):

```
set0 POV2 0xA4E690 0xA4E740   set0 POV3 0xA4EC10 0xA4ECC0   set0 POV4 0xA4F190 0xA4F240
set1 POV2 0xA4E7F0 0xA4E8A0   set1 POV3 0xA4ED70 0xA4EE20   set1 POV4 0xA4F2F0 0xA4F3A0
set2 POV2 0xA4E950 0xA4EA00   set2 POV3 0xA4EED0 0xA4EF80   set2 POV4 0xA4F450 0xA4F500
set3 POV2 0xA4EAB0 0xA4EB60   set3 POV3 0xA4F030 0xA4F0E0   set3 POV4 0xA4F5B0 0xA4F660
```

For a quick no-compile test, set these to MW's values in Cheat Engine (Close → 1.5/1.0, Far → 2.0/2.0, set2 POV2 → 2.0/0.5).

## Porting options, cheapest first

### A. Data patch ASI (prototype included)

[prototype/MWCamPatch.cpp](prototype/MWCamPatch.cpp) does the following:

- Finds the table array by pattern (`8D 04 BF 03 C1 8D 0C 80 8D 0C 8D` + imm32 in `Update`), so it isn't tied to hardcoded addresses.
- Restores `vEyeAccelMax.x`, using the preserved `vEyeAccelMin.x` to identify which MW POV each table came from.
- Optionally turns the brake-damping `je` at 0x491E02 into a `jmp` (pattern `0F 84 ? ? ? ? 38 9F B1 00 00 00 0F 85 ? ? ? ? 8B 8F 90 00 00 00 38 99 E1 00 00 00`). Note that a similar pattern also matches at 0x491D24, which is a different idle-camera block, hence the long pattern.
- **Not compiled.** No 32-bit compiler is installed here (MSYS2 only has ucrt64). Build it with MSVC x86 or `mingw32`.

Pair it with the MW `camerainfo` values (e.g. the [Most Wanted CAMERAS](https://nfsmods.xyz/mod/5349) VLT mod) so that both the rest position and the motion match.

### B. Full behaviour port: hook `CubicCameraMover::Update` / `SetDesired`

If A gets close but not all the way, replace Carbon's two functions with a reimplementation of MW's:

- **Reference implementation:** MW's own `Update`/`SetDesired`/`CameraAccelCurve`/`CameraSpeedHug` in `speed.exe` (addresses above). PS2 debug info gives every local's name (`collision_damper`, `drift_damper`, `gear_changing_table`, `speed_attenuation`, `lag_scale`, `envelope`, `eye_duration`, …), which makes decompiling them in Ghidra/IDA much easier. `Cubic.cpp` in the nfsmw decomp is still empty, so you'd be the one decompiling it.
- **Hook points:** the `CubicCameraMover` vtable `Update` slot in Carbon, or a direct detour at 0x4921B0. ThirteenAG's [NFSCarbon.WidescreenFix Camera.ixx](https://github.com/ThirteenAG/WidescreenFixesPack/blob/master/source/NFSCarbon.WidescreenFix/Camera.ixx) already hooks inside this function (pattern `E8 ? ? ? ? 8B 4E ? 8D 84 24 ? ? ? ? 50 51 E8` → `eCreateLookAtMatrix` call). **Your installed WSF will conflict** with any Update-level detour, so test with its `[CAMERA] Enable=0`.
- **Field offsets** differ: anchor at +0x90 instead of +0x80, `CameraAnchor` fields shifted (Carbon velMag at +0x90, top speed at +0x94, new bool flags at +0xE1/+0xE4). These need mapping. The `tCubic1D`/`tCubic3D` helpers appear unchanged, so call Carbon's.
- **Carbon-only features to preserve or bypass:** the `set` index (anchor+0x4, meaning unknown — possibly car class or race mode), the canyon/drift/crew-related extra calls in Update, and photo mode/ICE. The [Dynamic Camera FOV](https://nfsmods.xyz/mod/7046) ASI lists photo-mode and scripted-camera conflicts, so expect the same issues.

### C. Use the Carbon PS2 prototype to find what EA changed

TCRF says the Carbon OPS2M demo and the April 2006 footage reuse MW's camera system. If that prototype's ELF is obtainable, diffing its `CubicCameraMover` and table values against retail Carbon PS2 shows exactly when and what EA changed, including any Update logic beyond the table zeroing.

## Open questions / not verified

- **In-game result is untested.** Static analysis strongly suggests `EyeAccelMax.x` is the "MW swing-in" ([Antonio Moment's mod](https://nfsmods.xyz/mod/5349) notes "the zooming in effect is missing"), but that needs confirming in-game.
- **What anchor+0x4 (`set`) represents** and how it's chosen. Find the writer in the `CameraAnchor` update code.
- **The rest of Carbon's `Update`** (gear-change table, impact curves, ~20 extra calls) has not been diffed line by line.
- **Carbon's `+0x100 = 0.8f` write** in the brake block. Its purpose is unknown; it could be a FOV or blur factor.

## Sources

- [dbalatoni13/nfsmw](https://github.com/dbalatoni13/nfsmw): MW decomp and PS2/GC debug symbols (`Src/Camera/*`, `symbols/PS2/*`)
- [ThirteenAG WidescreenFixesPack: NFSCarbon Camera.ixx](https://github.com/ThirteenAG/WidescreenFixesPack/blob/master/source/NFSCarbon.WidescreenFix/Camera.ixx)
- [TsyVM/MWSDK mw05_camera.hpp](https://github.com/TsyVM/MWSDK): MW PC CubicCameraMover offsets
- [berkayylmao/NFSPluginSDK](https://github.com/berkayylmao/NFSPluginSDK): Carbon types (~40%, no camera yet)
- [TCRF: Prerelease Need for Speed: Carbon](https://tcrf.net/Prerelease:Need_for_Speed:_Carbon), [Proto: OPS2M Demo](https://tcrf.net/Proto:Need_for_Speed:_Carbon/OPS2M_Demo)
- [Most Wanted CAMERAS (Carbon)](https://nfsmods.xyz/mod/5349), [Extended Cameras](https://nfsmods.xyz/mod/915), [Dynamic Camera FOV for Carbon](https://nfsmods.xyz/mod/7046), [Performance Reworking](https://nfsmods.xyz/mod/6305)
