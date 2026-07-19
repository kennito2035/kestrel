#!/usr/bin/env python3
"""Summarize a Kestrel gate-telemetry capture.

Input: a serial capture (file argument or stdin) containing the shipping
firmware's UART lines, e.g. as echoed to USB serial by the RP2350 cascade
sketch. Only `gate,...` lines are parsed; everything else (boot banner,
DET events, stop/wake markers, `#` comments) is ignored, so a raw
Serial Monitor dump works as-is.

Line format (one line every 16 processed frames):
    gate,<frame>,<gated>,<skipped>,<changed>,<state>,<infer_ms>

The frame/skipped counters reset when gating is toggled (K1) or the
board reboots; segments are detected and summed.

Usage:
    python summarize.py capture.log
    python summarize.py < capture.log
"""
import sys


def main() -> int:
    src = open(sys.argv[1], encoding="utf-8", errors="replace") \
        if len(sys.argv) > 1 else sys.stdin

    frames = skipped = 0          # banked totals from finished segments
    seg_frames = seg_skipped = 0  # running totals of the current segment
    infer_ms = []
    det_events = 0
    lines = 0

    for line in src:
        line = line.strip()
        if line.startswith("DET,"):
            det_events += 1
            continue
        if not line.startswith("gate,"):
            continue
        parts = line.split(",")
        if len(parts) != 7:
            continue
        try:
            frame, _gated, skip = int(parts[1]), int(parts[2]), int(parts[3])
            ms = int(parts[6])
        except ValueError:
            continue
        lines += 1
        if frame < seg_frames:  # counter reset: bank the finished segment
            frames += seg_frames
            skipped += seg_skipped
        seg_frames, seg_skipped = frame, skip
        infer_ms.append(ms)

    frames += seg_frames
    skipped += seg_skipped

    if frames == 0:
        print("no gate,... lines found; is this a Kestrel serial capture?")
        return 1

    inferences = frames - skipped
    mean_ms = sum(infer_ms) / len(infer_ms)
    avg_cost = inferences * mean_ms / frames

    print(f"telemetry lines   : {lines}")
    print(f"frames            : {frames}")
    print(f"skipped           : {skipped}  ({100.0 * skipped / frames:.1f}%)")
    print(f"inferences        : {inferences}")
    print(f"inference latency : {min(infer_ms)}-{max(infer_ms)} ms "
          f"(mean {mean_ms:.1f})")
    print(f"avg per-frame cost: {avg_cost:.2f} ms "
          f"(vs {mean_ms:.0f} ms always-on: {mean_ms / avg_cost:.0f}x)")
    if det_events:
        print(f"DET events        : {det_events}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
