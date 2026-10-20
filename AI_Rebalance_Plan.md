# Career AI rebalance plan (NFS Carbon, Battle Royale install)

Status: in progress. Findings behind each point are in [MW_AI_Notes.md](MW_AI_Notes.md).

## Goals (agreed)

1. **No snoozefests, no tryhard requirement.** Races stay contested whatever car you drive. A casual player in a weak car
   can win with clean driving, and a maxed car doesn't make the field irrelevant.
2. **No tiers.** The modified roster makes tiers meaningless (MW-style), so difficulty can't come from tier. It has to
   follow **your car** (matching).
3. **Keep the wingman's torque boost** (up to +50%) and the leader slow-down as they are.
4. **Prefer ASI over database edits.** Edit the database only where a fix can't be done in code. Everything is switchable
   (ini) and tested before anything is written to the BR files.

## What decides how hard a career race is

| factor | where it comes from | ASI or database? |
|---|---|---|
| AI plan limits (how much of its own car the AI uses) | `ComputePotentials` floor, hardcoded | **ASI** (`AIBalance`: fixed floors, or `RelativeMargin` = your rating + margin) |
| AI skill | `character_racy` `SkillLevel 0` + career adaptive difficulty (save stat 6000, −1..1) | **ASI** clamp of the adaptive value (`AdaptiveMin/Max`). That caps skill at 0.7; going higher needs a `SkillLevel` above 0 (one database record, `character_racy`) or an ASI skill hook |
| catch-up (glue) | default: built-in strength-cap curves over race progress (fade in the last 25%), blended by the adaptive value; per-event curves only with `CatchUpOverride` | **ASI** (`CatchUpCapLow/High`, `CatchUpTorque`); database only for per-event curves |
| AI car (model, parts) | generic opponents fall back to `QR_AI_SKIN_1..20`; model and parts source **not found** | unknown until traced. If your car is faster than any AI car, only extra torque (catch-up cheat or override) can close the gap, and that's ASI |
| wingman override | separate catch-up path | keep as is |

So far, every lever except "skill above 0.7" can be done in ASI code.

## How matching works (MW's idea, done in Carbon's planner)

Carbon already plans each AI with `Lerp(min(AI car, your car), AI car, bias)` per axis. With
`bias = Ramp(your rating + margin, your rating, 1)` (`RelativeMargin`), each AI plans a fixed step above your car, up to its
own car's limit:

| your car (top / handling / accel) | margin 0.15 → bias |
|---|---|
| stock starter 0.30 / 0.39 / 0.18 | 0.21 / 0.25 / 0.18 |
| maxed 0.84 / 0.93 / 0.96 | 0.94 / 1 / 1 (the AI uses its whole car) |

Against a weak car the AI holds back to just above you. Against a strong car it uses everything it has. That's still not
enough for the maxed case: in the maxed runs the AI never led. So for strong cars the remaining levers are skill (above
0.6–0.7) and catch-up / extra torque. Those decide whether goal 1 holds at the top end.

## Phase 0: baseline (now)

- [x] Telemetry: race CSV gains `race_active` (0 from the moment you finish; cut gaps there) and `adaptive`.
- [ ] **Control run:** sf.5.4, stock car, `AIBalance.ini` all empty (stock behaviour). Installed this way now.
- [ ] Optional: one mid or late event with your usual car, to see a different field.

## Phase 1: matching (ASI, built, untested)

- [ ] `RelativeMargin=0.15`: sf.5.4 with the stock car, then the maxed car.
- [ ] Check in the race CSV that the bias equals the table above, then compare the finish margin and the share of the race
  each AI leads against the control run.

## Phase 2: keeping trailing AIs in it (ASI, built, untested)

The leader slow-down is intended and stays as is. The problem is trailing AIs giving up on a fast car late in the race.
The cause is that Carbon's catch-up cap fades over the last quarter of the race, harder when the adaptive value is low.

- [ ] `CatchUpCapLow=0.75` and `CatchUpCapHigh=0.75` (no late fade), maxed car on sf.5.4: does the AI stay in touch in lap 2?
- [ ] If the AI still can't hold on, raise `CatchUpTorque` (stock 0.5; 1.0 doubles the boost to about +30%).
- [ ] Fix the adaptive value (`AdaptiveMin = AdaptiveMax`) for every test, so runs are comparable.
- [ ] Slow car: `RelativeMargin` 0.05 and adaptive about 0.2 (skill ≈ 0.44) as the starting point.
- [ ] Later: NOS and pursuit telemetry, if leaders still fade unexplained.
- [x] Late fade confirmed and removed by `CatchUpCap` 0.75 (maxed run: trailing AIs held their gap, but couldn't close it).
- [ ] Slow car: AIs out-accelerate it 2–4× (their own cars, simplified physics). Try `TorqueMatch` 0.5–0.8.
- [x] Stock car, `TorqueMatch` 0.5: AI acceleration −30%; player won by 166–448 m.
- [x] Maxed car, `CatchUpTorque` 1.0: +30% torque when trailing; less lap-2 loss, but the gap opens in lap 1.
- [ ] `CatchUpSpreadLow` / `CatchUpSpreadHigh` about 150 m on both cars, so catch-up and slow-down react to small gaps.
- [ ] Then settle on one config for both cars.

## Phase 3: AI car source (only if Phases 1–2 can't close the gap)

- [ ] Log each AI car's model and parts, and trace where generic opponents get them.

## Validation

For each setting, sf.5.4 with the stock car and the maxed car (plus one other event when possible), comparing:
- finish order and margin, measured where `race_active` drops;
- the share of the race each AI leads;
- AI skill, bias and plan values.

Goal 1 holds when both cars produce a race where the AI leads for part of it and the finish is within a few hundred
metres, either way.
