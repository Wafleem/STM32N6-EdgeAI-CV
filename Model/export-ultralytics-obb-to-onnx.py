#!/usr/bin/env python3

import argparse
import shutil
from pathlib import Path

from ultralytics import YOLO


def main() -> None:
    parser = argparse.ArgumentParser(description="Export a Safal-style Ultralytics OBB checkpoint to ONNX.")
    parser.add_argument("checkpoint", help="Path to the source .pt checkpoint")
    parser.add_argument("output", help="Path to the output .onnx file")
    parser.add_argument("--imgsz", type=int, default=640, help="Square export size")
    parser.add_argument("--opset", type=int, default=13, help="ONNX opset")
    args = parser.parse_args()

    checkpoint = Path(args.checkpoint).resolve()
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    model = YOLO(str(checkpoint), task="obb")
    exported_path = Path(
        model.export(format="onnx", imgsz=args.imgsz, simplify=True, opset=args.opset)
    ).resolve()

    if exported_path != output:
        shutil.copyfile(exported_path, output)

    print(output)


if __name__ == "__main__":
    main()
