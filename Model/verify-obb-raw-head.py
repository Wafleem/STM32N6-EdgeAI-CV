#!/usr/bin/env python3

import argparse
import collections
from pathlib import Path

import onnx


POSTPROCESS_OPS = {
    "ArgMax",
    "Gather",
    "GatherElements",
    "Greater",
    "Less",
    "NonMaxSuppression",
    "RoiAlign",
    "Sort",
    "TopK",
    "Where",
}


def tensor_shape(value_info) -> list[int | str]:
    return [
        dim.dim_value if dim.dim_value else dim.dim_param
        for dim in value_info.type.tensor_type.shape.dim
    ]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Verify that a YOLO OBB ONNX exposes a raw MCU-decoded head."
    )
    parser.add_argument("model", help="Path to the ONNX model to verify")
    parser.add_argument("--classes", type=int, default=1, help="Expected class count")
    parser.add_argument("--boxes", type=int, default=2100, help="Expected candidate count")
    args = parser.parse_args()

    model_path = Path(args.model)
    model = onnx.load(model_path)
    ops = collections.Counter(node.op_type for node in model.graph.node)
    forbidden = {op: ops[op] for op in sorted(POSTPROCESS_OPS) if ops[op]}

    if len(model.graph.output) != 1:
        raise SystemExit(f"ERROR: expected one output, found {len(model.graph.output)}")

    output = model.graph.output[0]
    shape = tensor_shape(output)
    expected_channels = args.classes + 5

    if len(shape) != 3:
        raise SystemExit(f"ERROR: expected rank-3 output [1,{expected_channels},{args.boxes}], got {shape}")

    if int(shape[1]) != expected_channels or int(shape[2]) != args.boxes:
        raise SystemExit(f"ERROR: expected output channels/boxes {expected_channels}/{args.boxes}, got {shape}")

    if forbidden:
        raise SystemExit(f"ERROR: NPU-side postprocess ops remain: {forbidden}")

    print(
        f"OK: {model_path} exposes raw OBB head {shape}; "
        f"no NMS/TopK/selection postprocess ops found."
    )


if __name__ == "__main__":
    main()
