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

**Test event:** `race_bin_santafe/1_5_4` (`sf.5.4`, LocalizationTag 282), a 2-lap, 5.3 km circuit in Santa Fe with six
`character_racy` opponents, plus the wingman. The user knows it as Salazar Street in the Battle Royale install. It was
identified from the recorded start position: race CSV `(x, z)` = game `(−Y, X)`, so start (−1790, −2300) is
`startgrid` (−2290.5, 1790.2).

**A/B runs** (all stock-floor runs used `CareerFloor` 0.75 unless noted):

| run | player `p` (top/handling/accel) | AI bias | AI planned top speed | at the finish line |
|---|---|---|---|---|
| stock, 0.75 (`..._185751`) | 0.30 / 0.39 / 0.18 | 0.64 / 0.59 / 0.70 | 94.6–103.5 m/s | not checked (file deleted) |
| maxed, 0.75 (`..._190443`) | 0.84 / 0.93 / 0.96 | 0 / 0 / 0 | 101.6–115.7 m/s | recording stopped about 2 s before the finish; player leading, wingman 31 m back, others 630–920 m |
| stock, 0.5 (`..._191818`) | 0.30 / 0.39 / 0.18 | 0.29 / 0.18 / 0.39 | 90.6–92.0 m/s | **player first**: wingman 55 m back, others 170–300 m; individual AIs led for 16–47% of the race |
| maxed, 0.5 (`..._192942`) | 0.84 / 0.93 / 0.96 | 0 / 0 / 0 | 103.9–115.7 m/s | player first, AIs 110–470 m back, never led |

- Upgrades change `p` (it's estimated from the car's parts).
- With the maxed car the bias is 0, and `min(its car, yours)` is the AI's own car on almost every axis: the AI drives at full
  potential and is beaten on car and skill (0.6).
- The stock player has no NOS (`nos_capability` 0) but every AI got `Lerp(0, 1, bias)` of its own.
- **Correction:** the "jump" that every car made at the same point (112.0 s stock, 81.9 s maxed; 108 s in the first stock
  run) is the **player crossing the finish line**. The finish hands the player's car to its AI (the player's race action is
  rebuilt, 5–6 s pass without SuspensionRacer ticks, so the log clock stops), and `[GRaceStatus+0x6A1C]` stays null
  afterwards, so AIs that restart after the finish plan with bias 0. That's post-race, so it's harmless. Earlier
  "AI finished 300–1500 m ahead" figures were measured after the finish, while the player was on autopilot and the AIs
  kept racing; they're wrong. Also, `gap_m` integrates speed, so it's only meaningful up to the finish.
- The wingman (Sal; car 15 in the first maxed run, car 1 in the 0.5 stock run) runs on the catch-up override (below).

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

**Matching runs** (sf.5.4; finish where `race_active` drops):

| run | adaptive at start | AI skill | AI bias | AI planned top speed | result | AI lead share |
|---|---|---|---|---|---|---|
| control: stock car, stock floor (`..._201116`) | 0.583 | 0.753 | 0.64 / 0.59 / 0.70 | 101–106 m/s | player won; AIs 15–172 m behind | cars 4 and 7 led 99% until lap 2 |
| stock car, `RelativeMargin` 0.15 (`..._201646`) | 0.210 | 0.451 | 0.21 / 0.25 / 0.18 (as predicted) | 86–89 m/s | player about 6th; 5 AIs ahead by 0.5–181 m | 16–98% |
| maxed car, `RelativeMargin` 0.15 (`..._202216`) | 0.582 | 0.753 | 0.94 / 1 / 1 | 94–116 m/s | player won by 139–511 m; wingman 37 m **ahead** | 0% |

- **The adaptive value swings between consecutive races** (0.58 → 0.21 → 0.58), and AI skill swings with it (0.75 → 0.45 → 0.75).
  The fit is skill ≈ 0.28 + 0.81 × adaptive, so the base isn't 0. Which result moves it which way isn't clear yet: here it
  dropped after a win and rose after a loss, but restarts may count too. The skill difference makes the two stock runs only
  roughly comparable.
- **Slow-down glue is severe.** An AI 100 m or more ahead gets `glue_skill` about −0.64, so its effective skill
  (`base + glue`, clamped) is about 0.12 for as long as it leads (car 7 in the control run: 0.12 from 10 s to 80 s).
  Trailing AIs get +0.62 (effective 1.0) and catch-up cheat 0.19 (+14% torque).
- **Leaders slow down in lap 2 beyond the glue.** Control run, average mph per 250 m section:
  - car 7: 115 in lap 1, 99 in lap 2 (−14%), even though its average effective skill *rose* from 0.17 to 0.31;
  - car 4: −4%;
  - car 2, which spent time behind and got catch-up, was +6%.
  No `ComputePotentials` re-runs happened mid-race. Candidates: NOS running out (the catch-up cheat also speeds up AI NOS
  recharge, and leaders have cheat 0), and cops (`CopsInRace` is true in sf.5.4, `CopDensity` 70). Needs NOS and pursuit
  telemetry.
- **Catch-up fades out late, by design.** `GRaceStatus::ComputeCatchUpSkill` (0x643100, `ret 0x14`) has two modes. If the event
  sets `CatchUpOverride` (0x63EB20), it uses the event's `CatchUpSkill` / `CatchUpSpread` curves (+0xBBF0.., +0xBC20..,
  integral/derivative gains +0xBBE0/+0xBBE4). Otherwise, the default used by sf.5.4, it uses built-in tables over race
  progress x = `[racer info+0x3C]` (0..100), blended by A = clamp((adaptive + 1) × 0.5, 0, 1):
  - spread = Lerp(0xA61130 {1000, 900, 750, 750, 800}, 0xA61144 {300, 275, 250, 150, 250}, A) metres (at least 100);
  - cap = Lerp(0xA61158 {0.75, 0.75, 0.75, 0.5, 0}, 0xA6116C {0.75, 0.75, 0.75, 0.75, 0.25}, A);
  - glue = clamp(2 × error / spread + integral + derivative, −1, 1) × cap. Carbon then applies the catch-up glue table by
    base skill (≈ 0.83 at 0.75), so the measured +0.62 peak = 0.75 × 0.83.
  The cap falls over the last 25% of the race (to 0.20 at the finish with adaptive 0.58, and to 0 at −1), so trailing AIs
  stop catching up during lap 2 of a 2-lap race. That's the "AI can't catch the maxed car after lap 1" symptom, and why it
  depends on the adaptive value. The torque cheat (`clamp(base + glue − 1) × 0.5`, at most 0.19 = +14% torque here) fades with it.
  `AIBalance` `CatchUpCapLow` / `CatchUpCapHigh` / `CatchUpSpreadLow` / `CatchUpSpreadHigh` / `CatchUpTorque` rewrite these
  tables.
- **The maxed car outruns AIs at full potential and full skill.** Effective skill 1.0 plus the catch-up torque isn't
  enough; the AI never led.
- **`CatchUpCap` 0.75 and adaptive pinned at 0.6, maxed car (`..._203908`):** the glue held 0.63 and the cheat 0.20 right to
  the finish, and the late fade was gone. Trailing AIs stopped losing ground in lap 2 (car 2: −752 m at 60 s, −723 m at 80 s;
  car 5 finished 13 m back), but none could close. AI skill 0.767.
- **`TorqueMatch` 0.5, stock car (`..._205522`, otherwise as `..._204257`):** normal AIs' full-throttle acceleration fell
  about 30% (90–110 mph: 9–10 mph/s, from 12–16), while the wingman's override kept him at about 17. The player won by
  166–448 m (Sal finished 59 m ahead) and the AIs led 9–31% of the race.
- **`CatchUpTorque` 1.0, maxed car (`..._205917`, otherwise as `..._203908`):** the trailing cheat rose to 0.40 (+30% torque) and
  AI acceleration above 130 mph rose. Trailing AIs lost 28 m on average in lap 2 (73 m before), but the gap opens in lap 1
  (300–480 m by half distance): the maxed car pulls about 36 mph/s at 50–70 mph against the AI's 23, and with a spread of
  about 440 m the glue only reaches full strength once an AI is roughly 200 m back. Traffic hits were rare (0–2 sudden
  non-braking losses per car). Hence `CatchUpSpread`.
- **`RelativeMargin` 0.05 and adaptive 0.2, stock car (`..._204257`):** bias 0.07 / 0.08 / 0.06, skill 0.442. The player
  finished third (Sal +48 m, car 7 +37 m); the other AIs were 84–219 m behind and led 9–34% of the race.
- **AI racers always run SuspensionSimple** (`physics` 2 on every row, even alongside the player), and they accelerate far
  harder than the stock car whatever the bias. Median-to-upper full-throttle mph/s:
  - 70–90 mph: player stock 5–6, AIs 13–19;
  - 90–110 mph: 4 against 12–19;
  - 110–130 mph: 3.5 against 10–17.
  The maxed player car is 31 / 21 / 17 in those bands, and Sal at +75% torque reaches 28–33. AI acceleration was the same
  in the control run (bias 0.64) and the 0.05 run (bias 0.07), so the plan limits don't touch it. It's the AI car itself.
- **Where the catch-up cheat goes** (engine and suspension use the `ICheater` stored at `+0x110` / `+0xDC`):
  - engine torque × (1 + 0.75 × cheat) at 0x7007CF; constant 0x9C2D70 = 0.75, so the earlier "× 0.5" was wrong;
  - NOS drain × (1 − 0.5 × cheat) at 0x6E4015, and recharge × (1 + cheat) at 0x6E414E;
  - SuspensionSimple 0x7047D3 passes (1 − cheat) and (1 + 0.5 × cheat) to 0x6DEBB0;
  - SuspensionRacer (racer class only, 0x6FCB76) uses 0.23 + 0.27 × cheat.
  Nothing clamps it, so a negative value cuts torque. `AIBalance` `TorqueMatch` replaces the 5-byte `mov eax,[ecx]; call
  [eax+4]` at 0x7007D9 with a call that subtracts `TorqueMatch × (1 − best player Acceleration rating)` for
  `AIVehicleRacecar` cheaters (vtable 0x9C4D40) in career, unless the override at `+0x54` is active (the wingman). The
  floor is −1, so torque never goes below 25%.

**Floor 0.5 test:** see the A/B table. The bias came out at exactly the predicted values. With the stock car the race
became close, and the player won, but there's no valid 0.75 control yet (that run's file was deleted before the finish
line was checked). Repeat it before drawing conclusions.

**The AI cars are about as strong as your fully upgraded one.** Every AI car is rated exactly 1.0 / 1.0 / 1.0. Working back
from `plan = min + (own − min) × bias`, their own potentials are about 101–116 m/s top speed and 2.0–2.1 / 8.9–10.9 g grip.
Your maxed car is 115.7 m/s and 2.1 / 10.9; the stock car is 81.9 and 1.75 / 5.1. So in this tier 1 event the opponents
drive cars on par with a maxed player car.

Where the rating comes from: the PVehicle factory (constructor call at 0x6DAE34, constructor 0x6DA530, `ret 0x24`, rating
pointer is its 8th argument) uses `VehicleParams+0x34` if the creator set it, otherwise (flag 0x8 in `+0x38`) estimates it
from the car and its parts with 0x6CE330, otherwise none. The player's rating changes with upgrades, so it's an estimate.
GripTelemetry now logs every vehicle's source and rating, plus the call chain from the stack, in `vehicles_Carbon.csv`.
**Answered (`vehicles_Carbon.csv`, `sf.5.4`):**
the player's rating is `estimated`; all seven racers' is `explicit` 1.0 / 1.0 / 1.0. The racer creator (around 0x629C00,
return address 0x629E8E) hardcodes it: it stores 1.0 three times on the stack (0x629DA7–0x629DB7) and passes that pointer
as `matched` to the VehicleParams constructor 0x4048A0. That's stock exe code, not the mod. As far as traced, an AI's own
rating isn't read by `ComputePotentials` (best *player* only) or the glue (player's `TopSpeed`), so the 1.0 says nothing
about how strong the AI car is.

The car itself: in career (`[GRaceStatus+0x6A18] == 3`) the creator takes a preset ride name from the opponent's record
(`[racer info+0x1D0]`, read at 0x629CCC) and builds the customization from it (0x7C2F70 finds the preset, then 0x7D6EE0 /
0x7E1880 / 0x7C4040). With no preset it falls back to `QR_AI_SKIN_<(a + b) % 20 + 1>` (format string 0x9D87C8).

**Event data (BR database, read-only YAML export of a copy via Attribulator `unpack`):** almost every Santa Fe career event
uses `character_racy` opponents (`SkillLevel 0`, no `PresetRide`, no `CarType`, `MinimumAIPerformance 0`) and inherits
`DifficultyLevel 100`, `CatchUp true`, `CatchUpSkill 0.2` and `PlayerCarPerformance 1` from `race` / `basicrace`. The only
exceptions are story and test-drive races (sf.1.1 test drives at skill 40, sf.2.1 at `DifficultyLevel 40` with skill-60
Sal presets, sf.6.4 against `character_weak`, bosses). So these opponents use the `QR_AI_SKIN` fallback, which is a visual
preset. The `presetride` VLT (performance part levels 0–10) has no `QR_AI_SKIN` entries, so their **car model and
performance parts come from somewhere else that hasn't been found yet**. Measured strength is on par with the player's
maxed car.

**Difficulty and skill in career:**
- `GRaceParameters::GetDifficulty` (0x63C5C0) buckets `DifficultyLevel` into 0 (< 34), 1 (34–66) or 2 (≥ 67), but
  `ComputeSkill` only uses it for quick race (tables 0x9C1B90 / 0x9C1B9C).
- Career skill = `clamp(SkillLevel × 0.01 + adaptive × table(base))`, with the up table {0.7, 0.9, 1} when adaptive > 0 and
  the down table {0.5, 0.2, 0.2} otherwise.
- `adaptive` is `GRaceStatus+0xBBE8` (0x6127D0). It's loaded from persistent career stat 6000 (0x6127F0) and rewritten
  after each career race by the result code at 0x6429C0–0x642F13 (margins, clamped to −1..1). The career catch-up skill
  (0x643131) also scales with `(adaptive + 1)`.
- With `character_racy`'s `SkillLevel 0`, AI skill comes entirely from this: the measured 0.597 = 0.853 × 0.7. So the
  career's adaptive difficulty, which is persisted in the save, decides how well every generic opponent drives.

**`AIBalance.asi` `RelativeMargin`** redirects `ComputePotentials`' three `Ramp` calls (0x42F5F6 acceleration → `+0x84`,
0x42F60C handling → `+0x80`, 0x42F622 top speed → `+0x7C`). In career, when the floor passed in is one of the two career
immediates, it uses `rating + margin` instead. **`AdaptiveMin/Max`** redirects ComputeSkill's only call to 0x6127D0
(0x40A963) and clamps the result.

**`AIBalance.asi` scaling:** `CarStrength` = clamp((mean of the best player's three ratings − `RatingWeak`) /
(`RatingStrong` − `RatingWeak`), 0, 1). The margin in `RelativeRamp` and the adaptive value returned at 0x40A963 become
Lerp(AtWeak, AtStrong, strength) and are read at race start (`ComputePotentials`, `ComputeSkill`). The stock starter
(mean 0.29) and the maxed car (0.91) sit at the two ends with the shipped 0.3 / 0.9.

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
- `GetCatchupCheat = clamp(base + glue − 1, 0, 1) × 0.5`. In Carbon the engine applies it as torque × `(1 + 0.75 × cheat)` (up to +37.5%; the wingman's override at 1.0 gives +75%), plus NOS changes.

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
  519 m behind. That's the wingman (Sal was in both races; `AIActionRaceWingman`, `AIGoalWingmanRacer`). The writer
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
- **Race CSV extras:** `race_active` (`[GRaceStatus+0x6A1C]` set; drops to 0 when the player finishes) and `adaptive`
  (`GRaceStatus+0xBBE8`).
- **`potentials_Carbon.csv`** (always on, appended): one line per `ComputePotentials` call with the call site (`construct` =
  0x436005 in the action constructor, `start` = 0x4361E9), the car, `[GRaceStatus+0x6A18]`, whether `+0x6A1C` is set, both
  flag queries, the best player rating, and the bias and planned top speed that came out.
- **`vehicles_Carbon.csv`** (always on, appended): one line per PVehicle created: driver class (0 human, 3 racer), COM key,
  rating source (`explicit` / `estimated` / `none`), the rating, and up to 8 return addresses of direct calls on the stack.
- **Hooks:** grip-scale call at 0x703C05 and 0x7046E8; `ICheater::GetCatchupCheat` vtable slots 0x9C4ABC and 0x9C4D44; `ComputePotentials` calls at 0x436005 and 0x4361E9; PVehicle constructor call 0x6DAE34 and estimator call 0x6DADA7. Cars are matched by their COM object (`[ISimable+4]` = `[ICheater+4]` = `[IVehicle+4]`).
- **No sound under Wine:** the start/stop `Beep` is silent there, so check for new CSVs in `scripts/` instead.
- **Limit:** cars that are off-world (no physics behaviour running) produce no rows until they're back in range.
