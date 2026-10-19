# Grip: MW (speed.exe v1.3) vs Carbon (NFSC.exe v1.4): static diff

Method: MW decomp source (`SuspensionRacer.cpp`, `Chassis.cpp`) → matching functions in both PC exes by constants, tables and xrefs → normalized instruction diff.

## Same in both games

- **Data tables:** load-sensitivity curves (0–12°), `SteeringRange` / `SteeringSpeed` / `SteeringWheelRange` / `SteeringInputRange`, `DriftRearFriction`, `GripVsSpeed`, `TractionVsSpeed`.
- **Tire tweak globals:** `MaxSlipAngle`=12, `BrakingTorque`=4, `EBrakingTorque`=10, `RollingFriction`=2, `WheelMomentOfInertia`=10, `BrakeSteeringRangeMultiplier`=1.45, and the yaw limits.
- **`Tire::ComputeLateralForce`** (MW 0x68E090 / Carbon 0x6DF480): same code, same `GripFactor` (2500), same DRM `pLatForceMultipliers` use.
- **`Tire::GetPilotFactor`, `CheckForBrakeLock`, `UpdateFree`, `CheckSign`:** identical size and code.
- **`Chassis::ComputeTractionScale`** (MW 0x68D640 / Carbon 0x6DE920): `TractionVsSpeed × 1.1`.
- **`DoDrifting`** for normal races: MW's constants and the `mDrift.Value` gate. Carbon only adds a separate parameter set for drift events (`driver_style == 2`).

## Different

### 1. `Chassis::ComputeLateralGripScale`: biggest suspect

- **MW (0x68D5F0):** `return GripVsSpeed(speed / 85 mph) * 1.2f` → about 1.0 at standstill, about 1.24 at 35–60 mph, 1.2 at 85+ mph.
- **Carbon (0x6DE890):** computes the same `GripVsSpeed × 1.2` (plus a 0/1 flag read from the vehicle), then returns **1.0f if it's non-zero, otherwise 0.0f**. In practice it **always returns 1.0**.
- **Effect:** the result goes into every tire's `BeginFrame(..., grip_boost, ...)` as `mGripBoost`, which multiplies `ComputeLateralForce`. So Carbon tires make about 17–20% less cornering force than MW at normal speeds.
- **Caveat:** this scales cornering *stiffness* (lateral force per slip angle). It does not scale the hard cap `load × STATIC_GRIP × traction × pilot`. Surface `LATERAL_GRIP` scales the final force *after* the cap, so the old +30% surface mod only approximated this.

### 2. `Tire::UpdateLoaded` (MW 0x69DA90, 0x792 bytes / Carbon 0x6EE920, 0x98F bytes)

- **Traction circle removed.** In MW it was only driven by the handling tuning slider (lateral ×(1 + 0.2·h), longitudinal ×(1 − 0.2·h)), so at default tuning it has no effect.
- **The 1.5× "accelerating" force ellipse is removed from the normal path.** In MW, longitudinal force under power is counted ×1.5 against the grip limit, and kept ×1.5 if saturated. Carbon has neither, so under throttle at the limit it has more lateral grip but less drive force than MW.
- **New burnout hack:** when speed is above 2 and several vehicle-state conditions are met, max force ×0.2, slip ×10 and longitudinal force ×6.
- **New per-tire friction-ellipse mode:** flag `+0x124`, separate longitudinal and lateral limits `+0x128`/`+0x12C`, set via `SetEllipse` at 0x6DF080. It's **only enabled in drift events** or when a global at 0xA9E65B is set; normal races use the MW-like path.

### 3. Smaller

- `ComputeMaxSlip` in reverse gear: MW 71 → Carbon 1.5.

## Carbon `Tire` layout (from the constructor at 0x6EE810 and `BeginFrame` at 0x6DF3F0)

| field | MW | Carbon |
|---|---|---|
| mAV | 0xD0 | 0xCC |
| mLoad | 0xD4 | 0xD0 |
| mLateralForce | 0xD8 | 0xD4 |
| mLongitudeForce | 0xDC | 0xD8 |
| mLateralBoost | 0xE8 | 0xE4 |
| mTractionBoost | 0xEC | 0xE8 |
| mSpecs | 0x100 | 0xFC |
| mAxleIndex | 0x10C | 0x108 |
| mTraction | 0x110 | 0x10C |
| mSlipAngle | 0x118 | 0x114 |
| mTractionCircle | 0x11C | (removed) |
| mMaxSlip | 0x124 | 0x118 |
| mGripBoost | 0x128 | 0x11C |
| mDriftFriction | 0x12C | 0x120 |
| ellipse flag / long / lat | — | 0x124 / 0x128 / 0x12C |
| mBrakeLocked | 0x134 | 0x134 |

`SuspensionRacer::mTires[4]` is at MW +0x1B4 / Carbon +0x1C0. `Chassis::State` has the same layout in both games (speed +0xBC, driver_style +0xC8, driver_class +0xCA, gear +0xCC).

## Not checked yet

- **Car data:** per-car `tires` attributes (`STATIC_GRIP`, `DYNAMIC_GRIP`, `GRIP_SCALE`, `YAW_CONTROL`). Carbon may have retuned these to compensate for #1.
- **The rest of `SuspensionRacer`:** `DoWheelForces`, steering and yaw-control code beyond the shared globals.

## Telemetry: [prototype/GripTelemetry.cpp](prototype/GripTelemetry.cpp)

- **Setup:** works in both games. It hooks the `ComputeLateralGripScale` call in `SuspensionRacer::OnTaskSimulate` (MW 0x6B1F79 / Carbon 0x703C05) and logs only the human-driven car. F10 starts and stops a recording; each recording becomes a new `grip_<game>_<time>.csv` next to the .asi.
- **Columns:** speed, inputs, car slip angle, yaw rate, lateral/longitudinal g, gear, the grip scale the game produced, the DRM lateral-force multiplier, and each tire's load, forces, traction, slip angle, grip boost, traction boost, drift friction and Carbon ellipse state.
- **Summary:** `python analyze_grip.py <csv files>` prints p95 lateral g, grip scale, grip boost and % sliding per speed band.

## Test procedure

1. Use the same car in both games (e.g. RX-8, Gallardo or Murciélago), stock tuning, normal race or free roam, on dry asphalt.
2. Press F10, do the same set of corners: a long constant-radius bend at about 50 mph and about 90 mph, plus a quick slalom. Press F10 again.
3. Expected if #1 is the cause: Carbon's `grip_scale` and `*_gripboost` columns read 1.000 where MW reads about 1.2, and Carbon's p95 lateral g is lower in the 40–100 mph bands.

## Round 2: low-speed handling and launches (MW install checked: code/data identical to the analysed exe)

**Same in both games** for normal races (`driver_style` != 2):
- `CalculateMaxSteering`: Carbon only adds a drift-event multiplier.
- `CalculateSteeringSpeed`
- `Burnout::Update`
- `TuneWheelParams`: staging ×0.25, the burnout trigger and yaw control are unchanged. Carbon only drops drag-race branches and the tuning traction circle.

Steering averages are already time-based in both games.

**`DoDriveForces`** (MW 0x68F0D0 / Carbon 0x6E04E0), burnout branch:
- MW: spinning wheel traction boost ×T (`BurnoutFriction`), other driven wheel ×1.0.
- Carbon: ×2T and ×2.0, so no launch wheelspin.
- Fix: `RestoreBurnoutTraction` (on). Constants at 0x6E0787/0x6E07B9 → 1.0, `fadd st,st` at 0x6E0795/0x6E07CD → NOP.

**`DoWheelForces`** (MW 0x6AA020 / Carbon 0x6FA390), torque resolve:
- MW: `yaw × YAW_SPEED` only.
- Carbon also: `pitch ×` chassis +0x6C (pitch > 0) or +0x7C (pitch ≤ 0), and `roll ×` chassis +0x54. These are Carbon-only chassis fields that a ported MW car can't carry.
- Fix: `RemovePitchRollScaling` (off until the telemetry shows the values). NOPs at 0x6FB164/0x6FB169/0x6FB1B6.

**Not examined:** a game-mode-4-only force plus the new subsystem at 0x6F80B0, called before `DoDriveForces` in `OnTaskSimulate`.

**Telemetry v2 columns:** steering angle and limit, wheel angles, yaw control, drift and burnout state/traction, `YAW_SPEED`, Carbon pitch/roll scales, and per tire wheel surface speed, road speed, slip and drive torque.

## Round 3: Cobalt SS recordings at SimRate -1 (165 Hz in both games)

**Launch** (`RestoreBurnoutTraction` on): matches MW. Front wheelspin is 16–17 m/s off the line, with the burnout state cutting one front wheel to about 0.7 traction in both games, and grip returns around 30 mph. Carbon spins about 0.3–0.8 s longer, and its wheels are slightly faster at launch (17.4 vs 16.2 m/s).

**Car data matches MW:**
- standing load 12,490 N, 50/50 front/rear, and the same loads at speed;
- tire `STEERING` coefficient 1.05;
- `YAW_SPEED` 0.40;
- Carbon pitch/roll scales 1.0, so `RemovePitchRollScaling` does nothing for this car;
- the same tire μ vs slip curves (peak front 1.68, rear about 1.75).

**`DoHumanSteering`** (MW 0x69E820 / Carbon 0x6EF540) was rewritten in Carbon:
- It branches on the steering device type.
- For a gamepad it uses a new `state+0xDC` input (from `IInput` slot +0x14, clamped to ±1). When the input has the same sign, it adds `|input| × d × 45°` (limit raised to ±70°). When the sign is opposite, it multiplies steering by `(1 − |d|)`.
- The recordings show no sign of it being active for this player (the over-limit share is the same as MW's), so `d` is probably 0.

**Input:** the steering input reaching physics swept 10→90% in a median 0.07 s in MW vs 0.46 s in Carbon. There's no game-side rate limit, since Carbon input can jump 0.84 in one tick. The XtendedInput deadzone differs: `PercentLS` is 0.24 in MW vs 0.08 in Carbon.
