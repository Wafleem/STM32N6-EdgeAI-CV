#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image


def square_pad_rgb(image: Image.Image, pad_value: int = 114) -> Image.Image:
    width, height = image.size
    side = max(width, height)
    canvas = Image.new("RGB", (side, side), (pad_value, pad_value, pad_value))
    x0 = (side - width) // 2
    y0 = (side - height) // 2
    canvas.paste(image, (x0, y0))
    return canvas


def preprocess_uint8_chw(path: Path, imgsz: int) -> np.ndarray:
    image = Image.open(path).convert("RGB")
    padded = square_pad_rgb(image)
    resized = padded.resize((imgsz, imgsz), Image.Resampling.BILINEAR)
    hwc = np.asarray(resized, dtype=np.uint8)
    chw = np.transpose(hwc, (2, 0, 1))
    return chw.reshape(-1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Prepare ST Edge AI validation inputs as flattened uint8 .npy batches. "
            "Each row is one preprocessed image tensor with shape [3,imgsz,imgsz]."
        )
    )
    parser.add_argument("images_dir", help="Root directory containing validation images")
    parser.add_argument("output_npy", help="Output .npy file with shape [N, 3*imgsz*imgsz]")
    parser.add_argument("--manifest", help="Optional CSV manifest of selected image paths")
    parser.add_argument("--imgsz", type=int, default=320, help="Square model input size")
    parser.add_argument("--count", type=int, default=10, help="Number of images to include")
    args = parser.parse_args()

    images_dir = Path(args.images_dir).resolve()
    output_npy = Path(args.output_npy).resolve()
    output_npy.parent.mkdir(parents=True, exist_ok=True)

    image_files = sorted(
        path for ext in ("*.jpg", "*.jpeg", "*.png") for path in images_dir.rglob(ext)
    )
    selected = image_files[: args.count]

    if not selected:
        raise SystemExit(f"No images found under {images_dir}")

    batch = np.stack([preprocess_uint8_chw(path, args.imgsz) for path in selected], axis=0)
    np.save(output_npy, batch, allow_pickle=False)

    manifest = Path(args.manifest).resolve() if args.manifest else output_npy.with_suffix(".csv")
    with manifest.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["index", "source_image"])
        for index, path in enumerate(selected):
            writer.writerow([index, str(path)])

    print(f"Wrote {output_npy}")
    print(f"shape={batch.shape} dtype={batch.dtype} min={int(batch.min())} max={int(batch.max())}")
    print(f"manifest={manifest}")


if __name__ == "__main__":
    main()
