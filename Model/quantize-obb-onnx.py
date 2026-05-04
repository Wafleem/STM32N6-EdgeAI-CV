#!/usr/bin/env python3

import argparse
from pathlib import Path

import onnx
import numpy as np
from onnx import TensorProto, helper
from onnxruntime.quantization import (
    CalibrationDataReader,
    QuantFormat,
    QuantType,
    quantize_static,
)


class NpyCalibrationDataReader(CalibrationDataReader):
    def __init__(self, samples_dir: Path, input_name: str):
        self._samples = iter(sorted(samples_dir.glob("*.npy")))
        self._input_name = input_name

    def get_next(self):
        try:
            sample_path = next(self._samples)
        except StopIteration:
            return None

        tensor = np.load(sample_path).astype(np.float32, copy=False)
        return {self._input_name: tensor}


def rewrite_qdq_boundary(src_path: Path, dst_path: Path) -> None:
    model = onnx.load(src_path)

    rewritten_nodes = []
    for node in model.graph.node:
        if node.name == "images_QuantizeLinear" or (
            node.op_type == "QuantizeLinear" and list(node.output) == ["images_QuantizeLinear_Output"]
        ):
            continue
        if node.name == "output0_DequantizeLinear" or (
            node.op_type == "DequantizeLinear" and list(node.output) == ["output0"]
        ):
            continue
        rewritten_nodes.append(node)

    del model.graph.node[:]
    model.graph.node.extend(rewritten_nodes)

    del model.graph.input[:]
    model.graph.input.extend(
        [helper.make_tensor_value_info("images_QuantizeLinear_Output", TensorProto.INT8, [1, 3, 640, 640])]
    )

    del model.graph.output[:]
    model.graph.output.extend(
        [helper.make_tensor_value_info("output0_QuantizeLinear_Output", TensorProto.INT8, [1, 8, 8400])]
    )

    kept_value_info = []
    for value_info in model.graph.value_info:
        if value_info.name in {"images", "output0"}:
            continue
        kept_value_info.append(value_info)
    del model.graph.value_info[:]
    model.graph.value_info.extend(kept_value_info)

    onnx.checker.check_model(model)
    onnx.save(model, dst_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Quantize a Safal-style OBB ONNX model with static int8 calibration.")
    parser.add_argument("input_model", help="Path to the source float ONNX model")
    parser.add_argument("calibration_dir", help="Directory containing prepared .npy calibration tensors")
    parser.add_argument("qdq_output", help="Path to the quantized QDQ ONNX output")
    parser.add_argument("--int8-boundary-output", help="Optional path to emit an int8-boundary QDQ ONNX")
    args = parser.parse_args()

    input_model = Path(args.input_model).resolve()
    calibration_dir = Path(args.calibration_dir).resolve()
    qdq_output = Path(args.qdq_output).resolve()

    reader = NpyCalibrationDataReader(calibration_dir, "images")
    quantize_static(
        model_input=str(input_model),
        model_output=str(qdq_output),
        calibration_data_reader=reader,
        quant_format=QuantFormat.QDQ,
        activation_type=QuantType.QInt8,
        weight_type=QuantType.QInt8,
    )

    if args.int8_boundary_output:
        rewrite_qdq_boundary(qdq_output, Path(args.int8_boundary_output).resolve())

    print(qdq_output)


if __name__ == "__main__":
    main()
