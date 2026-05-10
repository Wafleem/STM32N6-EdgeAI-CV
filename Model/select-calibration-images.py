#!/usr/bin/env python3

import argparse
import csv
import shutil
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Select a clean calibration subset from audited image metrics.")
    parser.add_argument("metrics_csv", help="CSV produced by audit-calibration-images.py")
    parser.add_argument("output_dir", help="Directory where selected images will be copied")
    parser.add_argument("--count", type=int, default=300, help="Maximum selected image count")
    parser.add_argument("--max-block-jump", type=float, default=30.0)
    parser.add_argument("--max-dark", type=float, default=0.08)
    parser.add_argument("--max-bright", type=float, default=0.12)
    parser.add_argument("--max-saturation", type=float, default=0.14)
    args = parser.parse_args()

    with open(args.metrics_csv, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    def metric(row: dict[str, str], name: str) -> float:
        return float(row[name])

    clean = [
        row
        for row in rows
        if metric(row, "block_jump") <= args.max_block_jump
        and metric(row, "dark") <= args.max_dark
        and metric(row, "bright") <= args.max_bright
        and metric(row, "saturation") <= args.max_saturation
    ]

    # Spread across the sorted list instead of taking a burst from one video segment.
    clean = sorted(clean, key=lambda row: row["path"])
    if len(clean) > args.count:
        selected = [clean[round(i * (len(clean) - 1) / (args.count - 1))] for i in range(args.count)]
    else:
        selected = clean

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    for old_file in output_dir.glob("*.jpg"):
        old_file.unlink()

    for index, row in enumerate(selected):
        src = Path(row["path"])
        dst = output_dir / f"{index:04d}_{src.name}"
        shutil.copy2(src, dst)

    print(f"eligible={len(clean)} selected={len(selected)} output={output_dir}")


if __name__ == "__main__":
    main()
