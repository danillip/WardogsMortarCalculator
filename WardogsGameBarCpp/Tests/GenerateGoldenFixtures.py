"""Development-only oracle. Run only after the user's first build/approval.
Uses the unmodified upstream ballistics.py; Python is not shipped in the MSIX.
"""
from pathlib import Path
import csv
import random
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "upstream" / "wardogs-mortar-calculator"))
from ballistics import MORTAR_TABLE, calculate_ballistics

output = root / "artifacts" / "golden.csv"
output.parent.mkdir(parents=True, exist_ok=True)
cases = [(0, 0, 0, 0)]
for distance, _ in MORTAR_TABLE:
    for delta in (-0.001, 0, 0.001):
        for dx, dy in ((0, 1), (1, 0), (-1, 0), (0, -1)):
            cases.append((100, 100, 100 + dx * (distance + delta) / 100, 100 + dy * (distance + delta) / 100))
rng = random.Random(2026)
cases.extend(tuple(rng.uniform(0, 20) for _ in range(4)) for _ in range(2000))
with output.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.writer(stream)
    for coords in cases:
        r = calculate_ballistics(*coords)
        writer.writerow((*coords, r.distance_m, r.azimuth_deg, r.elevation_mil or 0,
                         {"OK": 0, "TOO_CLOSE": 1, "TOO_FAR": 2}[r.range_status], r.dx_m, r.dy_m))
print(output)
