# AI difficulty in Carbon (with MW-ported car values): step 1 findings

## Database: `aivehicle/racers` (children may override)

| field | stock Carbon (CE disc) | your install |
|---|---|---|
| TopSpeedMultiplier | 1.0 | 1.3 |
| AccelerationMultiplier | 1.0 | 1.3 |
| MAXIMUM_AI_SPEED | 400 | 400 |

- **In MW** (speed.exe 0x427040, an AI driving/planning function), these are optional attributes looked up by key (`0xEC57E16B`, `0xCC320329`). They scale the AI's **planned** top speed (car's real top speed × multiplier, capped by `MAXIMUM_AI_SPEED`) and its **planned** acceleration curve. They don't change engine output; the planner uses them to choose target speeds and braking points.
- **In Carbon**, those keys never appear in code. The `aivehicle` record is kept at `PVehicle+0x124` (constructor around 0x43B913), so the fields are read by fixed position. Where Carbon applies them is **not mapped yet**.

## Catch-up ("glue")

**MW** (decomp `AIVehicle.cpp`):
- Once a second, `glue_error = race_length/100 × (avg player % complete − AI % complete) × ((1 − player perf.TopSpeed) × 0.5 + 1)`, so a lower-*rated* player car means stronger glue.
- `GRaceStatus::ComputeCatchUpSkill` (a controller with per-race skill, spread and two gains from race data) produces `fGlueSkill`, scaled by base-skill tables (catch-up `{0.33, 0.66, 1.0}`, slow-down `{1.0, 1.0, 0.66}`).
- Skill = `clamp(fBaseSkill + fGlueSkill)`. `fBaseSkill` comes from the difficulty setting and the opponent's character skill (adaptive up/down tables).
- `GetCatchupCheat = clamp(base + glue − 1, 0, 1) × 0.5`, which becomes AI engine torque × `(1 + cheat × 0.5)` (up to +25%) plus faster NOS recharge.

**Carbon:**
- `AIPerpVehicle::GetCatchupCheat` is at 0x40AE80 (vtable slot 0x9C4ABC, the one ExtraOptions' `NoCatchUp` patches). It's the same formula and the same 0.5 table (0xA4C2AC), **plus** a Carbon-only negative override (`[cheater+0x54] < 0` → return it), which **cuts** AI torque when set.
- The quick-race catch-up option is `DALOptions::GetCatchUp` at 0x4A59C4.

## Also

`MWGripPatch`'s `RestoreGripScale` changes `Chassis::ComputeLateralGripScale`, which AI racers also use (the `SuspensionSimple` call at 0x7046E8). So AI cornering grip rose about 20% along with the player's.

## Race telemetry (GripTelemetry, Carbon only)

- F10 now also writes `race_Carbon_<time>.csv`: one row per car (human + `DRIVER_RACER`) every 0.05 s.
- **Columns:** speed, distance driven since F10, gap to the player (+ = ahead), world position, gas/brake/gear, physics type (1 = SuspensionRacer, 2 = simplified), `base_skill`, `glue_skill`, `catchup_cheat`, `cheat_override`, and the age of the catch-up sample.
- **Hooks:** grip-scale call at 0x703C05 and 0x7046E8; `ICheater::GetCatchupCheat` vtable slot 0x9C4ABC. Cars are matched by their COM object (`[ISimable+4]` = `[ICheater+4]`).
- **Limit:** cars that are off-world (no physics behaviour running) produce no rows until they're back in range.
