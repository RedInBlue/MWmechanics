# MWmechanics

Restores Need for Speed: Most Wanted (2005) mechanics in Need for Speed: Carbon, using ASI plugins found by
reverse engineering both executables. The [nfsmw decompilation](https://github.com/dbalatoni13/nfsmw) and MW's
PS2 debug symbols serve as the reference.

**Target:** `NFSC.exe` v1.4 (7,217,152 bytes). MW reference: `speed.exe` v1.3 (6,029,312 bytes).
Every patch checks the original bytes before writing anything. On a different exe it does nothing instead of crashing,
so if a patch seems to have no effect, compare your exe's size and hash first.

## Plugins (`prototype/`)

| ASI | What it restores | ini switches |
|---|---|---|
| `MWCamPatch` | MW chase camera: the eye swings in toward the car under braking; Carbon's brake damping removed | `RestoreEyeSwing`, `SkipBrakeDamping` |
| `MWTachPatch` | Needle shows audio RPM (smooth upshifts); redline shake at MW speed regardless of SimRate | `UseAudioRPM`, `SmoothRedlineShake`, `RedlineBounce` |
| `MWGripPatch` | MW lateral grip scale (about 1.2 at speed), throttle force ellipse, launch wheelspin, Carbon-only hacks removed | `RestoreGripScale`, `RestoreThrottleEllipse`, `RemoveBurnoutHack`, `RestoreBurnoutTraction`, `RemovePitchRollScaling` |
| `AIBalance` | Configurable floor for the AI performance bias (Carbon hardcodes 0.75 in career, 1.0 elsewhere), so the AI scales with a weak player car | `CareerFloor`, `CareerFloorAlt`, `OtherFloor` |
| `GripTelemetry` | F10 records the player's handling (`grip_*.csv`) and, in Carbon, every racer (`race_*.csv`, including catch-up and AI performance-bias state). Works in MW and Carbon | `ToggleKey` |

Install: copy the `.asi` (plus an optional `.ini` with the same name) into the game's `scripts` folder. This needs the
Ultimate ASI Loader (`dinput8.dll`). Settings that worked (all 1 except `RemovePitchRollScaling` and `RedlineBounce`) are
listed in each `.cpp` header and the notes.

## Notes

- [MW_Camera_Port_Research.md](MW_Camera_Port_Research.md): camera (`CubicCameraMover` lag tables)
- [MW_Tach_Notes.md](MW_Tach_Notes.md): tachometer (DAL RPM, `SoundCar`, redline bounce timing)
- [MW_Grip_Notes.md](MW_Grip_Notes.md): tire model, steering, launch, `DoWheelForces`, and the comparison recordings
- [MW_AI_Notes.md](MW_AI_Notes.md): AI difficulty (catch-up / glue, `aivehicle` multipliers). **Work in progress.**
- `Restore_Stock_LATERAL_GRIP.nfsms`: stock surface grip values (from the CE disc)

## Building

32-bit only (the games are x86).

```bash
# Linux (Debian/Ubuntu: apt install g++-mingw-w64-i686 · Fedora: mingw32-gcc-c++ · Arch: mingw-w64-gcc)
cd prototype
i686-w64-mingw32-g++ -shared -O2 -static -s -o AIBalance.asi AIBalance.cpp
i686-w64-mingw32-g++ -shared -O2 -static -s -o GripTelemetry.asi GripTelemetry.cpp
i686-w64-mingw32-g++ -shared -O2 -static -s -o MWCamPatch.asi MWCamPatch.cpp
i686-w64-mingw32-g++ -shared -O2 -static -s -masm=intel -o MWGripPatch.asi MWGripPatch.cpp
i686-w64-mingw32-g++ -shared -O2 -static -s -o MWTachPatch.asi MWTachPatch.cpp
```

On Windows, use MSYS2's `mingw-w64-i686-gcc` (`C:\msys64\mingw32\bin\g++.exe`) with the same flags.
`-static` matters: without it the ASIs depend on MinGW runtime DLLs.

## Running under Linux (Wine/Proton)

- The ASI loader needs a DLL override: `WINEDLLOVERRIDES="dinput8=n,b"`, or set `dinput8` to *native, builtin* in `winecfg` for that prefix.
- The ASIs are plain Win32 and behave the same. The F10 hotkey works; the telemetry beeps may be silent.
- CSVs are written next to the `.asi` inside the prefix.

## Tools (`tools/`)

Python 3 plus `objdump` (binutils), with no other dependencies. Work in a scratch folder that holds copies of the exes,
**never committed**, named `mw_speed.exe` and `nfsc.exe` (`datadiff.py` and `patchlist.py` expect those names).

| script | purpose |
|---|---|
| `re/pe.py` | minimal PE loader: VA↔file offset, `?` wildcard patterns |
| `re/find.py "<pattern>" exe...` | byte-pattern search, prints VAs |
| `re/callers.py exe <hexva>` | direct `call` sites of a function |
| `re/xref.py exe <lo> <hi>` | code references to a data address range |
| `re/fstart2.py exe <hexva>...` | function bounds around an address (int3-padding based; beware stray `0xCC` bytes) |
| `re/funcs.py exe <lo> <hi>` | list functions and sizes in a range (pair MW and Carbon listings to spot changed functions) |
| `re/dump.py exe <va>:<count>...` | dump floats |
| `re/vtw.py exe <lo> <hi>` | constructor vtable writes into a vtable range |
| `re/strref.py` / `re/str.py` | string → code refs / string at VA |
| `re/fieldw.py exe <off>` | writes to a struct offset |
| `re/datadiff.py`, `re/tables.py`, `re/povtables.py`, `re/patchlist.py` | data-block and table diffs used for the camera and grip work |
| `telemetry/analyze_grip.py csv...` | per-speed-band summary of `grip_*.csv` |
| `telemetry/launch.py`, `telemetry/lowspeed.py` | launch timeline and low-speed steering/grip comparisons |
| `vlt/marker_readback.py` | read database values via Attribulator ModScripts (no export needed); see the docstring |

Typical diff workflow: `objdump -d -M intel --start-address=... --stop-address=... nfsc.exe`, normalize
addresses and offsets, and `diff` against the MW function (as in the notes).

## Current status

The handling, camera and tachometer were tested in-game and feel 1:1 with MW on the MW-ported Cobalt SS.

Next: AI balance. Record an early and a late career race with the telemetry (`race_Carbon_*.csv`), find where Carbon
applies `aivehicle` `TopSpeedMultiplier`/`AccelerationMultiplier` (currently 1.3, stock 1.0), then build `AIBalance.asi`
(match AI pace to the player's car performance, plus catch-up control).
