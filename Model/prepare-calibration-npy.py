#!/usr/bin/env python3

import argparse
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


def preprocess_image(path: Path, imgsz: int) -> np.ndarray:
    image = Image.open(path).convert("RGB")
    padded = square_pad_rgb(image)
    resized = padded.resize((imgsz, imgsz), Image.Resampling.BILINEAR)
    array = np.asarray(resized, dtype=np.float32) / 255.0
    chw = np.transpose(array, (2, 0, 1))
    return np.expand_dims(chw, axis=0)


def main() -> None:
    parser = argparse.ArgumentParser(description="Prepare calibration tensors from image files.")
    parser.add_argument("images_dir", help="Root directory containing calibration images")
    parser.add_argument("output_dir", help="Directory to write .npy tensors into")
    parser.add_argument("--imgsz", type=int, default=640, help="Square model input size")
    parser.add_argument("--max-images", type=int, default=192, help="Maximum images to convert")
    args = parser.parse_args()

    images_dir = Path(args.images_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    image_files = sorted(
        path for ext in ("*.jpg", "*.jpeg", "*.png") for path in images_dir.rglob(ext)
    )
    selected = image_files[: args.max_images]

    if not selected:
        raise SystemExit(f"No images found under {images_dir}")

    for index, image_path in enumerate(selected):
        tensor = preprocess_image(image_path, args.imgsz)
        np.save(output_dir / f"sample_{index:04d}.npy", tensor, allow_pickle=False)

    print(f"Prepared {len(selected)} calibration tensors in {output_dir}")


if __name__ == "__main__":
    main()
