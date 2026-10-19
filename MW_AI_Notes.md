# AI difficulty in Carbon (with MW-ported car values): step 1 findings

## Database: `aivehicle/racers` (children may override)

| field | stock Carbon (CE disc) | your install |
|---|---|---|
| TopSpeedMultiplier | 1.0 | 1.3 |
| AccelerationMultiplier | 1.0 | 1.3 |
| MAXIMUM_AI_SPEED | 400 | 400 |

- **In MW** (speed.exe 0x427040, an AI driving/planning function), these are optional attributes looked up by key (`0xEC57E16B`, `0xCC320329`). They scale the AI's **planned** top speed (car's real top speed × multiplier, capped by `MAXIMUM_AI_SPEED`) and its **planned** acceleration curve. They don't change engine output; the planner uses them to choose target speeds and braking points.
- **In Carbon**, those keys never appear in code. The `aivehicle` record is kept at `PVehicle+0x124` (constructor around 0x43B913), so the fields are read by fixed position. The race action's setup (0x435E60) caches them: layout `+0x10` → `AIActionRace+0xC0` (TopSpeedMultiplier, used in `GetPotentialSpeed` 0x416950), `+0x18` × 0.2778 → `+0xC4` (MAXIMUM_AI_SPEED in m/s), `+0x0C` → `+0xC8` (AccelerationMultiplier, used in `GetPotentialAcceleration` 0x426030).
- The Battle Royale install (`Desktop/NFSC/Need for Speed Carbon BR`) has more `aivehicle` racer records, with the multipliers reportedly reset and one at 0.2. Values not read back yet; the race CSV's `topspeed_mult`/`accel_mult` show what each racer actually got.

## Performance bias: why a weak player car makes the AI harder

Symptom: early career with a slow starter car the AI is unbeatable, while with a strong car it is easy and drives badly.

**MW** (decomp `AIActionRace::ComputePotentials` / `GetPotentialSpeed` / `GetPotentialAcceleration`): each AI racer plans its
top speed, cornering grip (start/end lateral g), acceleration and NOS as

```
planned = Lerp(min(AI car, player car), AI car, bias)        // per axis
bias    = UMath::Ramp(min_perf, p, 1.0) = clamp((min_perf - p) / (1 - p), 0, 1)   // 0 if 1 - p <= epsilon
```

`p` is the best player's `Physics::Info::Performance` rating on that axis (TopSpeed, Handling, Acceleration). With an AI car
rated 1.0, the AI ends up rated `max(p, min_perf)`: it never plans below `min_perf`, however slow the player's car is. MW only
does this in career, with `min_perf = GCharacter::MinimumAIPerformance()` (per opponent, from `gameplay`). Outside career the
bias is `Default()` = 0, so the AI never plans beyond the player's car.

**Carbon** `AIActionRace::ComputePotentials` = 0x42F4F0. It's called only from the race action's setup (0x436005, 0x4361E9),
so it runs once each time the action starts. The constructor sets the bias to 0. Fields: bias TopSpeed/Handling/Accel at
`+0x7C/+0x80/+0x84`, start/end grip `+0x9C/+0xA0`, NOS capability `+0x88` (also at `+0x8C`), usable NOS `+0x90`, bottle time
`+0x98`, top speed `+0xA4`.

| case | `min_perf` |
|---|---|
| no race parameters (`[GRaceStatus+0x6A1C]` null) | bias = 0 |
| career (`[GRaceStatus+0x6A18] == 3`) | **0.75**; 0.7 when 0x63D720 is true; 1.0 when 0x63E180 is true |
| any other context (quick race = 0) | 1.0, so bias = 1 and the AI always plans with its own car (unless p ≥ 1, which gives 0) |

Hardcoded instead of per opponent. With a 0.75 floor in every career race:

| player p | 0.2 | 0.4 | 0.6 | 0.7 | ≥ 0.75 |
|---|---|---|---|---|---|
| bias | 0.69 | 0.58 | 0.38 | 0.17 | 0 |

- **Weak car:** the AI keeps most of its own car's potential, so it plans like a 0.75-rated car.
- **Strong car:** the bias is 0, so the AI plans with `min(its car, yours)` on every axis (top speed, grip, acceleration, NOS).
  It drives below what its car can do: easy, and visibly slow in corners.

Also unidentified: 0x63E180 also forces career skill to 1.0 (`ComputeSkill` 0x40A7F0), so it is probably a boss race flag.
0x63D720 also halves skill after +0.2.

**Confirmed in-game** (BR install, early career race, stock starter car, `race_Carbon_20261019_185250.csv`): player
`p` = (0.2965, 0.3939, 0.1789), every AI racer's `bias` = (0.6446, 0.5875, 0.6955), exactly `Ramp(0.75, p, 1)`. Every AI car
is rated 1.0 on all axes, so each AI plans at `p + (1 − p) × bias` = 0.75 on every axis. The racers read multipliers of 1.0
and a 111.1 m/s (400 km/h) cap, not the reported 0 / 0.2. The player car also runs `ComputePotentials` (it has an
AIActionRace too), so the human row shows bias and plan values; ignore them.

**A/B test, same early career event** (`race_Carbon_20261019_185751.csv` stock, `..._190443.csv` fully upgraded):

| | player `p` (top/handling/accel) | AI bias | AI planned top speed | result |
|---|---|---|---|---|
| stock | 0.30 / 0.39 / 0.18 | 0.64 / 0.59 / 0.70 | 94.6–103.5 m/s | 4 AIs 300–530 m ahead at 215 s |
| upgraded | 0.84 / 0.93 / 0.96 | 0 / 0 / 0 | 101.6–115.7 m/s | 6 of 7 AIs 630–920 m behind at 80 s |

- Upgrades do change `p`, so `matched` is computed from the tuned car (tracing who supplies it is no longer needed).
- With the upgraded car the AI plans with `min(its car, yours)` = its own car on almost every axis: it is driving at full
  potential and simply has the slower car. With the stock car it is held at 75% of the way from your car to its own.
- The stock player has no NOS (`nos_capability` 0) but every AI got 0.70 of its own: `Lerp(0, 1, bias)`.
- **AI resets drop the bias to 0.** Cars 1 and 4 in the stock run were repositioned (a 200 m jump at 108 s; stuck for 3 s and
  moved at 140 s). `ComputePotentials` ran again and came back with bias 0, so they planned at the player's level for the
  rest of the race and finished last. `0x420F00` (best player performance) is a plain max over the player list with no
  filter, so the 0 probably comes from the no-race-parameters branch (`[GRaceStatus+0x6A1C]` null at that moment). Unconfirmed.
  **Settled by `potentials_Carbon.csv`:** in both later runs every car, the player included, jumped 120–275 m forward at the
  same instant and the same place (around x −1788, z −2300; 112.1 s into the stock run, 81.9 s into the upgraded one), with
  5–6 s of wall time where the player's physics didn't tick. From that moment `[GRaceStatus+0x6A1C]` stays null for the
  rest of the race, so every later `ComputePotentials` takes the zero branch: any AI whose race action restarts after
  that point plans at `min(its car, yours)`. The first stock run's resets happened at the same spot. The player crashed
  into traffic there in both runs; the "jump" is just everyone driving on for 5–6 s while the player's SuspensionRacer
  wasn't ticking (the race log's clock only advances on player ticks), and the player's own race action was rebuilt.
  Why the crash leaves `+0x6A1C` null is not known.
- One AI in the upgraded run (car 15) stayed 30–220 m behind the player and hit 206–225 mph while the others topped out at
  170–187, which looks like catch-up. The catch-up columns were empty because racers don't use the vtable that was hooked
  (see Catch-up below).

**Where `p` comes from:** `PVehicle::GetPerformance` (IVehicle vtable 0x9E4A78, slot `+0xE8` = 0x6D8170) returns a copy cached in
the constructor (IVehicle `+0xF8..+0x100`, valid byte `+0x104`; the IVehicle subobject is at `PVehicle+0xB8`). The value is the
`VehicleParams::matched` pointer the car was created with, not a live estimate. If it's null, `GetPerformance` returns false:
the bias isn't updated (stays 0 from the constructor) and the glue performance ratio below is skipped. The A/B test above
shows the player's value reflects installed upgrades.

PerformaTor's standalone `operator()` (0x416900) is unreferenced; the copy actually used is inlined in `ForEach` 0x420F00.

**Carbon skill tables** (Table objects `{count, min, max, 1/step, data*}`, data in parentheses):

| table | Carbon | MW |
|---|---|---|
| NOS scale 0xA4C158 | {0.25, 1} | same |
| speed scale 0xA4C16C | {0.8, 1} | {0.85, 1} (+ drag table) |
| accel scale 0xA4C180 | {0.6, 1} | {0.65, 1} (+ drag {0.82, 1}) |
| catch-up accel 0xA4C194 | {1, 1.33, 1.66, 2} | same |
| cornering 0xA4C1A8 / 0xA4C1BC | {0.36, 0.9} / {0.34, 0.99} (second one used by `GetPotentialSpeed`) | {0.36, 0.9} |
| adaptive up / down 0xA4C234 / 0xA4C248 | {0.7, 0.9, 1} / {0.5, 0.2, 0.2} | {0.5, 0.75, 1} / {0.5, 0.375, 0.25} |
| quick race skill 0x9C1B90 / no glue 0x9C1B9C | {0.15, 0.4, 1} / {0.15, 0.4, 0.8} | same |
| catch-up glue 0xA4C25C, slow-down glue 0xA4C270 | {0.25, 0.66, 1}, {1, 1, 0.7} | {0.33, 0.66, 1}, {1, 1, 0.66} |
| glue when 0x63E120 is true 0xA4C284 / 0xA4C298 (else, if 0x404310 is true, × 0.5 both ways, like MW's speed trap) | {0.25, 0.66, 1} / {1, 0.2, 0.2} | none |
| catch-up cheat 0xA4C2AC | {0.5, 0.5, 0.5} | same |

**Floor 0.5 test** (`race_Carbon_20261019_191818.csv`, stock car, `CareerFloor=0.5`): bias came out at exactly
0.2892 / 0.1750 / 0.3910 as predicted, and planned top speed dropped from 94.6–103.5 to 90.6–92.0 m/s and end grip from
7.3–9.6 to 6.0–6.5. AI average speed barely moved (94–105 mph against 91–102 at 0.75), and they still finished 640–1500 m
ahead. The plan limits aren't what makes the AI fast against the stock car.

**The AI cars are about as strong as your fully upgraded one.** Every AI car is rated exactly 1.0 / 1.0 / 1.0. Working back
from `plan = min + (own − min) × bias`, their own potentials are about 101–116 m/s top speed and 2.0–2.1 / 8.9–10.9 g grip.
Your maxed car is 115.7 m/s and 2.1 / 10.9; the stock car is 81.9 and 1.75 / 5.1. So in this tier 1 event the opponents
drive cars on par with a maxed player car.

Where the rating comes from: the PVehicle factory (constructor call at 0x6DAE34, constructor 0x6DA530, `ret 0x24`, rating
pointer is its 8th argument) uses `VehicleParams+0x34` if the creator set it, otherwise (flag 0x8 in `+0x38`) estimates it
from the car and its parts with 0x6CE330, otherwise none. The player's rating changes with upgrades, so it's an estimate.
GripTelemetry now logs every vehicle's source and rating, plus the call chain from the stack, in `vehicles_Carbon.csv`.
**Answered (`vehicles_Carbon.csv`, Salazar Street, a Collector's Edition race that Battle Royale reuses as a career event):**
the player's rating is `estimated`; all seven racers' is `explicit` 1.0 / 1.0 / 1.0. The racer creator (around 0x629C00,
return address 0x629E8E) hardcodes it: it stores 1.0 three times on the stack (0x629DA7–0x629DB7) and passes that pointer
as `matched` to the VehicleParams constructor 0x4048A0. That's stock exe code, not the mod. As far as traced, an AI's own
rating isn't read by `ComputePotentials` (best *player* only) or the glue (player's `TopSpeed`), so the 1.0 says nothing
about how strong the AI car is.

The car itself: in career (`[GRaceStatus+0x6A18] == 3`) the creator takes a preset ride name from the opponent's record
(`[racer info+0x1D0]`, read at 0x629CCC; MW's `gameplay` class has the same `PresetRide` field) and builds the
customization from it (0x7C2F70 finds the preset, then 0x7D6EE0 / 0x7E1880 / 0x7C4040). Outside career, or if there's no
preset, it uses `QR_AI_SKIN_<1..20>`. So the AI cars' near-maxed strength comes from the **event data**: the opponents'
preset rides (their performance parts) in the Battle Royale database, not from code. That alone explains both halves of the symptom: the stock car is outclassed, and the maxed
car is level with the AI cars while the AI drives them at skill 0.6.

**`AIBalance.asi`** makes the floors configurable (`CareerFloor`, `CareerFloorAlt`, `OtherFloor`): it rewrites the immediates of
the `mov [esp+0x10], imm32` at 0x42F573 (0.75), 0x42F569 (0.7) and 0x42F57D (1.0, shared by non-career and the 0x63E180
career case). Restoring MW's per-opponent `MinimumAIPerformance` would need the character record instead of a constant.

## Catch-up ("glue")

**MW** (decomp `AIVehicle.cpp`):
- Once a second, `glue_error = race_length/100 × (avg player % complete − AI % complete) × ((1 − player perf.TopSpeed) × 0.5 + 1)`, so a lower-*rated* player car means stronger glue.
- `GRaceStatus::ComputeCatchUpSkill` (a controller with per-race skill, spread and two gains from race data) produces `fGlueSkill`, scaled by base-skill tables (catch-up `{0.33, 0.66, 1.0}`, slow-down `{1.0, 1.0, 0.66}`).
- Skill = `clamp(fBaseSkill + fGlueSkill)`. `fBaseSkill` comes from the difficulty setting and the opponent's character skill (adaptive up/down tables).
- Carbon keeps the performance ratio unchanged (glue function 0x430920, at 0x430B0D: `(1 − p.TopSpeed) × 0.5 + 1`, only when the
  player's `GetPerformance` succeeds), so a weak player car gets up to 1.5× glue on top of the bias above.
- `GetCatchupCheat = clamp(base + glue − 1, 0, 1) × 0.5`, which becomes AI engine torque × `(1 + cheat × 0.5)` (up to +25%) plus faster NOS recharge.

**Carbon:**
- `GetCatchupCheat` is at 0x40AE80. It's the same formula and the same 0.5 table (0xA4C2AC), **plus** a Carbon-only
  override: when `[cheater+0x54] >= 0` it's returned as is (−1 means off). The setter is the next vtable slot (0x40AE70).
  It is **not** capped at 0.5, so it can give the full +50% torque (corrected; earlier notes had the sign backwards).
- Two ICheater vtables hold it: 0x9C4ABC (vtable 0x9C4AB8, installed by the constructor around 0x43F100) and **0x9C4D44**
  (vtable 0x9C4D40, installed by the derived constructor around 0x43F6C0, which calls 0x43F100 and then sets the
  `AIGoalRacer` goal, so it's `AIVehicleRacecar`). Racers go through 0x9C4D44.
- **Measured** (both later runs): `base_skill` is a constant 0.597 for every racer in both runs (so career adaptive difficulty
  didn't move between attempts). `glue_skill` ranges −0.71 to +0.54, so the normal cheat peaks at
  (0.597 + 0.544 − 1) × 0.5 = 0.07 (+3.5% torque): weak. One racer per race used the override instead: 0.25–1.0 while it
  stayed within about ±50 m of the stock player for 100 s, and a constant 1.0 from 5 s in the upgraded run while it fell
  519 m behind. That's the wingman (Neville was in both races; `AIActionRaceWingman`, `AIGoalWingmanRacer`). The writer
  isn't traced yet.
- ExtraOptions' `NoCatchUp` only patches 0x9C4ABC (its ASI contains that address but not 0x9C4D44), so it most likely
  leaves career racers' catch-up untouched.
- The quick-race catch-up option is `DALOptions::GetCatchUp` at 0x4A59C4.

## Also

`MWGripPatch`'s `RestoreGripScale` changes `Chassis::ComputeLateralGripScale`, which AI racers also use (the `SuspensionSimple` call at 0x7046E8). So AI cornering grip rose about 20% along with the player's.

## Race telemetry (GripTelemetry, Carbon only)

- F10 now also writes `race_Carbon_<time>.csv`: one row per car (human + `DRIVER_RACER`) every 0.05 s.
- **Columns:** speed, distance driven since F10, gap to the player (+ = ahead), world position, gas/brake/gear, physics type (1 = SuspensionRacer, 2 = simplified), `base_skill`, `glue_skill`, `catchup_cheat`, `cheat_override`, and the age of the catch-up sample.
- **Performance columns:** `perf_top/handling/accel` is each car's own `GetPerformance` rating (empty if it has none; the human
  row is the `p` the AI used). AI rows also get `bias_*`, `plan_top_mps`, `plan_grip_start/end`, `nos_capability`, `usable_nos`,
  `topspeed_mult`, `max_ai_speed_mps`, `accel_mult`, as last computed by `ComputePotentials`.
- **`potentials_Carbon.csv`** (always on, appended): one line per `ComputePotentials` call with the call site (`construct` =
  0x436005 in the action constructor, `start` = 0x4361E9), the car, `[GRaceStatus+0x6A18]`, whether `+0x6A1C` is set, both
  flag queries, the best player rating, and the bias and planned top speed that came out.
- **`vehicles_Carbon.csv`** (always on, appended): one line per PVehicle created: driver class (0 human, 3 racer), COM key,
  rating source (`explicit` / `estimated` / `none`), the rating, and up to 8 return addresses of direct calls on the stack.
- **Hooks:** grip-scale call at 0x703C05 and 0x7046E8; `ICheater::GetCatchupCheat` vtable slots 0x9C4ABC and 0x9C4D44; `ComputePotentials` calls at 0x436005 and 0x4361E9; PVehicle constructor call 0x6DAE34 and estimator call 0x6DADA7. Cars are matched by their COM object (`[ISimable+4]` = `[ICheater+4]` = `[IVehicle+4]`).
- **No sound under Wine:** the start/stop `Beep` is silent there, so check for new CSVs in `scripts/` instead.
- **Limit:** cars that are off-world (no physics behaviour running) produce no rows until they're back in range.
