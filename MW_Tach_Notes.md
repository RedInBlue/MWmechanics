# Tachometer needle: why Carbon snaps on upshift and MW doesn't

**Cause:** MW's HUD shows the *audio* engine's RPM, while Carbon's HUD shows the *physics* engine's RPM.

## MW (speed.exe v1.3)

- In `LocalPlayer::UpdateHud` (tach section at 0x6F1521):
  - `rpm = IEngine::GetRPM()`
  - if not staging (and the driver class is not 1), and the car has `ICarAudio` (handle 0x694080): `rpm = ICarAudio::GetRPM()`
  - `ITachometer::SetRpm(rpm)` (vtable 0x8A2248, slot 1)
- `ICarAudio::GetRPM()` returns `SoundCar::mSoundRPM`. It's set in `SoundCar::OnService` as `svc.GetAudibleRPMPercent() * (redline - minrpm) + minrpm`, which is the sound engine's RPM and glides through gear changes.
- `Tachometer::Update` then just sets the needle rotation from that value, with no smoothing of its own (decomp `FeTachometer.cpp`).

## Carbon (NFSC.exe v1.4)

- The HUD tach class (vtable 0x9D6C50, update at 0x5DA340) reads RPM from the data layer: `DAL::GetFloat(2002)` at 0x4A0350 → vehicle handler switch at 0x4CC000 → `0x4B65F0`.
- `0x4B65F0` stores `IEngine::GetRPM()` (the physics `mRPM`, engine+0x120). It then **queries `ICarAudio` (handle 0x4A9D10) and discards the result.** MW's override is left in place as dead code.
- `SoundCar` still exists and still computes `mSoundRPM` (`OnService` at 0x6E54A0, `GetRPM` at 0x6F39C0 → `[this+0x30]`).
- `Engine_SmoothRPM` (MW 0x6927A0, Carbon 0x6E3C60) is identical in both games, so physics RPM smoothing isn't the difference.

## Fix: [prototype/MWTachPatch.cpp](prototype/MWTachPatch.cpp)

- Redirects the call at 0x4CC064 to a wrapper. The wrapper runs the original getter, then replaces the value with `ICarAudio::GetRPM()` when that's available. This is the same order MW uses.
- Byte-verified for v1.4 only. Turn it off with `UseAudioRPM=0` in `MWTachPatch.ini`.
- **Not ported:** MW's "not staging / driver class" condition. Carbon has no drag staging, so this probably doesn't matter.

## Redline needle shake

- **MW:** `SFXCTL_Engine::UpdateRPM` (MW PC ~0x4BC870, Carbon 0x510DF0, identical code). While redlining, i.e. the audio RPM on the 1000–10000 scale is above 9800 (`UpdateRedlining`, MW 0x4BC6A0 / Carbon 0x510C30), it ramps a "visual offset" toward 200 and back to 0 repeatedly and subtracts it from the visual RPM. The HUD shows that value through `SoundCar::mSoundRPM`.
- **The trigger:** MW's `SoundCar::OnService` maps physics RPM over `IDLE..RED_LINE`, clamped. The player has no rev limiter outside drag staging, so the audio RPM saturates at the redline and the bounce runs.
- **Carbon:** `SoundCar::OnService` (0x6E54A0) maps over `IDLE..MAX_RPM` when a vehicle check at `+0x5C`, call `+0x4C`, fails (most likely "uses rev limiter"). The audio RPM then never passes 9800, so the bounce never starts.
- **Fix** (`RedlineBounce=1` in `MWTachPatch.ini`): `je` → `jmp` at 0x6E5510, so it always uses `RED_LINE` like MW. Side effect: the engine **sound** pitch also tops out at the redline, as in MW, instead of rising toward max RPM.

## Redline shake speed

- **The real difference:** the shake already ran in Carbon, just too fast. `smooth(cur, target, 50)` (0x50AA90) is a fixed step per call, not per second, so a bounce cycle is always 8 `UpdateRPM` ticks. MW ran at 60 Hz, giving about 133 ms per cycle. With WidescreenFix `SimRate = -2` (2× refresh, about 330 Hz in the telemetry), the cycle drops to about 24 ms and reads as a buzz.
- **Fix** (`SmoothRedlineShake=1`): both `smooth()` calls (0x510F3E, 0x510F7D) go through a stub that multiplies the step by `dt × 60`. `dt` is `UpdateRPM`'s argument, at `[esp+0x24]` inside the stub. The stub keeps the original step if `dt × 60` falls outside (0, 4].
- `RedlineBounce` is now off by default. It only matters for cars whose shake never starts.
