#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image


def image_metrics(path: Path) -> tuple[float, float, float, float]:
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    gray = image.mean(axis=2)
    saturation_like = ((image.max(axis=2) - image.min(axis=2)) > 90.0).mean()
    dark = (gray < 20.0).mean()
    bright = (gray > 235.0).mean()

    # JPEG/mosaic-corrupted frames tend to have strong block-to-block luminance jumps.
    h, w = gray.shape
    block_h = max(1, h // 16)
    block_w = max(1, w // 16)
    cropped = gray[: block_h * 16, : block_w * 16]
    blocks = cropped.reshape(16, block_h, 16, block_w).mean(axis=(1, 3))
    block_jump = (
        np.abs(np.diff(blocks, axis=0)).mean() + np.abs(np.diff(blocks, axis=1)).mean()
    ) / 2.0
    return float(saturation_like), float(dark), float(bright), float(block_jump)


def main() -> None:
    parser = argparse.ArgumentParser(description="Audit calibration images for obvious bad calibration inputs.")
    parser.add_argument("images_dir", help="Directory containing calibration images")
    parser.add_argument("--top", type=int, default=12, help="Number of suspect images to print per metric")
    parser.add_argument("--csv", help="Optional CSV output path for all image metrics")
    args = parser.parse_args()

    images_root = Path(args.images_dir)
    images = sorted(images_root.rglob("*.jpg"))
    if not images:
        raise SystemExit(f"No .jpg images found in {args.images_dir}")

    rows = []
    for path in images:
        saturation, dark, bright, block_jump = image_metrics(path)
        rows.append(
            {
                "name": path.name,
                "path": str(path),
                "saturation": saturation,
                "dark": dark,
                "bright": bright,
                "block_jump": block_jump,
            }
        )

    print(f"images={len(rows)}")
    for key in ("block_jump", "saturation", "dark", "bright"):
        print(f"\nTop {args.top} by {key}:")
        for row in sorted(rows, key=lambda r: r[key], reverse=True)[: args.top]:
            print(f"{row[key]:.4f} {row['name']}")

    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=["path", "name", "saturation", "dark", "bright", "block_jump"])
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nwrote {args.csv}")


if __name__ == "__main__":
    main()
