#!/usr/bin/env python3
"""Cross-language golden check for the Kestrel motion gate.

Recomputes the 500 seeded fuzz cases from test_gate.c in pure Python
(bit-identical LCG and frame construction) and compares against the
results.csv the C test binary wrote. A third, independent implementation:
if C and Python agree on all 500 cases, a shared misconception in the two
C implementations is effectively ruled out.

Usage:  ./test_gate && python3 golden.py
"""
import csv
import sys
from pathlib import Path

W, H = 160, 120
PIXEL_THRESHOLD = 25
OPEN_COUNT = 96
ROI_PAD = 8

MASK = 0xFFFFFFFF


class Lcg:
    def __init__(self, seed: int):
        self.state = seed & MASK

    def rb(self) -> int:
        self.state = (self.state * 1664525 + 1013904223) & MASK
        return (self.state >> 24) & 0xFF


def clamp_u8(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def make_frames(seed: int):
    """Bit-for-bit replica of make_frames() in test_gate.c."""
    rng = Lcg(seed)
    prev = bytearray(rng.rb() for _ in range(W * H))
    curr = bytearray(clamp_u8(prev[i] + (rng.rb() % 21) - 10)
                     for i in range(W * H))
    if rng.rb() % 4 != 0:
        bw = 4 + rng.rb() % 40
        bh = 4 + rng.rb() % 40
        bx = rng.rb() % (W - bw)
        by = rng.rb() % (H - bh)
        for y in range(by, by + bh):
            for x in range(bx, bx + bw):
                if rng.rb() % 2:
                    i = y * W + x
                    curr[i] = clamp_u8(curr[i] + 60)
    return prev, curr


def gate_check(prev, curr):
    """From-spec Python implementation. Returns (state, count, x, y, w, h)."""
    count = 0
    xmin, xmax, ymin, ymax = W, -1, H, -1
    for y in range(H):
        row = y * W
        for x in range(W):
            if abs(curr[row + x] - prev[row + x]) > PIXEL_THRESHOLD:
                count += 1
                xmin = min(xmin, x)
                xmax = max(xmax, x)
                ymin = min(ymin, y)
                ymax = max(ymax, y)
    if count == 0 or count < OPEN_COUNT:
        return 0, count, 0, 0, 0, 0

    x0, x1 = xmin - ROI_PAD, xmax + ROI_PAD
    y0, y1 = ymin - ROI_PAD, ymax + ROI_PAD
    side = min(max(x1 - x0 + 1, y1 - y0 + 1), min(W, H))
    # C integer division truncates toward zero; Python // floors.
    # All midpoints here are non-negative-safe except when x0+x1 < 0,
    # which padding can produce near the origin; use int() truncation.
    rx = int((x0 + x1) / 2) - side // 2
    ry = int((y0 + y1) / 2) - side // 2
    rx = max(0, min(rx, W - side))
    ry = max(0, min(ry, H - side))
    return 1, count, rx, ry, side, side


def main() -> int:
    csv_path = Path(__file__).parent / "results.csv"
    if not csv_path.exists():
        print("results.csv not found; build and run ./test_gate first")
        return 1

    mismatches = 0
    with open(csv_path, newline="") as f:
        rows = list(csv.DictReader(f))
    if len(rows) != 500:
        print(f"expected 500 rows, got {len(rows)}")
        return 1

    for row in rows:
        seed = int(row["seed"])
        prev, curr = make_frames(seed)
        expect = gate_check(prev, curr)
        got = tuple(int(row[k]) for k in ("state", "count", "x", "y", "w", "h"))
        if got != expect:
            mismatches += 1
            print(f"MISMATCH seed {seed}: C={got} python={expect}")

    if mismatches:
        print(f"{mismatches} mismatch(es)")
        return 1
    print("golden check passed: C and Python agree on all 500 cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
