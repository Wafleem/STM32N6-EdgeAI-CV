#!/usr/bin/env python3
"""Summarize STM32N6 TRACE: perf serial logs.

The firmware prints lines like:

TRACE: perf: frame=1050 det=2 loop=67ms/14.9fps headless_est=58ms/17.2fps nn=39ms/25.6fps compute=44ms/22.7fps prep=2 post=3 uvc_display=9 cam_wait=14
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
from pathlib import Path


PERF_RE = re.compile(
    r"TRACE: perf: frame=(?P<frame>\d+) det=(?P<det>-?\d+) "
    r"loop=(?P<loop_ms>\d+)ms/(?P<loop_fps>\d+(?:\.\d+)?)fps "
    r"headless_est=(?P<headless_ms>\d+)ms/(?P<headless_fps>\d+(?:\.\d+)?)fps "
    r"nn=(?P<nn_ms>\d+)ms/(?P<nn_fps>\d+(?:\.\d+)?)fps "
    r"compute=(?P<compute_ms>\d+)ms/(?P<compute_fps>\d+(?:\.\d+)?)fps "
    r"prep=(?P<prep_ms>\d+) post=(?P<post_ms>\d+) "
    r"uvc_display=(?P<uvc_display_ms>\d+) cam_wait=(?P<cam_wait_ms>\d+)"
)


FIELDS = [
    "loop_ms",
    "loop_fps",
    "headless_ms",
    "headless_fps",
    "nn_ms",
    "nn_fps",
    "compute_ms",
    "compute_fps",
    "prep_ms",
    "post_ms",
    "uvc_display_ms",
    "cam_wait_ms",
]


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = (len(ordered) - 1) * pct
    lower = int(index)
    upper = min(lower + 1, len(ordered) - 1)
    weight = index - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize(values: list[float]) -> dict[str, float]:
    if not values:
        return {"min": 0.0, "mean": 0.0, "p95": 0.0, "max": 0.0}
    return {
        "min": min(values),
        "mean": statistics.fmean(values),
        "p95": percentile(values, 0.95),
        "max": max(values),
    }


def parse_log(path: Path) -> list[dict[str, float]]:
    rows: list[dict[str, float]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = PERF_RE.search(line)
        if not match:
            continue
        row: dict[str, float] = {}
        for key, value in match.groupdict().items():
            row[key] = float(value) if "." in value else int(value)
        rows.append(row)
    return rows


def markdown_table(summary: dict[str, dict[str, float]]) -> str:
    lines = [
        "| Metric | Min | Mean | P95 | Max |",
        "| :----- | --: | ---: | --: | --: |",
    ]
    for field in FIELDS:
        stats = summary[field]
        lines.append(
            f"| `{field}` | {stats['min']:.2f} | {stats['mean']:.2f} | {stats['p95']:.2f} | {stats['max']:.2f} |"
        )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Parse STM32N6 TRACE: perf logs.")
    parser.add_argument("log", type=Path, help="Serial log file")
    parser.add_argument("--markdown", action="store_true", help="Print a Markdown summary table")
    parser.add_argument("--json", action="store_true", help="Print JSON instead of text")
    args = parser.parse_args()

    rows = parse_log(args.log)
    if not rows:
        raise SystemExit(f"No TRACE: perf lines found in {args.log}")

    summary = {field: summarize([float(row[field]) for row in rows]) for field in FIELDS}
    detections = [int(row["det"]) for row in rows]
    detected_frames = sum(1 for count in detections if count > 0)
    meta = {
        "samples": len(rows),
        "first_frame": int(rows[0]["frame"]),
        "last_frame": int(rows[-1]["frame"]),
        "detected_frames": detected_frames,
        "detected_frame_ratio": detected_frames / len(rows),
    }

    if args.json:
        print(json.dumps({"meta": meta, "summary": summary}, indent=2))
        return

    if args.markdown:
        print(f"Samples: `{meta['samples']}`")
        print(f"Frames: `{meta['first_frame']}` to `{meta['last_frame']}`")
        print(f"Detected-frame ratio: `{meta['detected_frame_ratio']:.2%}`")
        print()
        print(markdown_table(summary))
        return

    print(f"Parsed {meta['samples']} TRACE: perf samples from {args.log}")
    print(f"Frames {meta['first_frame']} to {meta['last_frame']}")
    print(f"Detected-frame ratio: {meta['detected_frame_ratio']:.2%}")
    for field in FIELDS:
        stats = summary[field]
        print(
            f"{field:16s} min={stats['min']:.2f} mean={stats['mean']:.2f} "
            f"p95={stats['p95']:.2f} max={stats['max']:.2f}"
        )


if __name__ == "__main__":
    main()
