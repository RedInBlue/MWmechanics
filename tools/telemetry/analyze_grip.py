"""Summarize GripTelemetry CSVs: cornering grip per speed band, so MW and Carbon runs can be compared.

Usage: python analyze_grip.py grip_MW_*.csv grip_Carbon_*.csv
"""
import csv
import sys

BANDS = [(20, 40), (40, 60), (60, 80), (80, 100), (100, 130), (130, 250)]


def pct(values, p):
    if not values:
        return float("nan")
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p))]


def summarize(path):
    rows = list(csv.DictReader(open(path, newline="")))
    print(f"\n== {path}  ({rows[0]['game'] if rows else '?'}, {len(rows)} ticks)")
    print(f"{'mph':>9} {'ticks':>6} {'lat_g p95':>9} {'grip_scale':>10} {'gripboost F/R':>14} {'sliding F/R %':>14} {'driftfric R':>11}")
    for lo, hi in BANDS:
        band = [r for r in rows if lo <= float(r["speed_mph"]) < hi and int(r["gear"]) > 1]
        if not band:
            continue
        lat = [abs(float(r["lat_g"])) for r in band]
        corner = [r for r in band if abs(float(r["lat_g"])) > 0.3]
        def mean(key, rs):
            vals = [float(r[key]) for r in rs if r[key] != ""]
            return sum(vals) / len(vals) if vals else float("nan")
        def sliding(prefixes, rs):
            n = sum(1 for r in rs for p in prefixes if r[f"{p}_traction"] != "" and float(r[f"{p}_traction"]) < 0.999)
            return 100.0 * n / max(1, len(rs) * len(prefixes))
        rs = corner or band
        print(f"{lo:>4}-{hi:<4} {len(band):>6} {pct(lat, 0.95):>9.3f} {mean('grip_scale', band):>10.3f} "
              f"{mean('fl_gripboost', band):>6.3f}/{mean('rl_gripboost', band):<6.3f} "
              f"{sliding(['fl', 'fr'], rs):>6.1f}/{sliding(['rl', 'rr'], rs):<6.1f} {mean('rl_driftfric', rs):>11.3f}")
    mult = {r["latforce_mult"] for r in rows}
    print(f"latforce_mult values seen: {sorted(mult)}")


for p in sys.argv[1:]:
    summarize(p)
