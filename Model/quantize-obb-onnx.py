#!/usr/bin/env python3

import argparse
import os
import tempfile
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


_TENSOR_TYPES = {
    "int8": TensorProto.INT8,
    "uint8": TensorProto.UINT8,
}


def _tensor_shape(value_info) -> list[int | str]:
    dims = []
    for dim in value_info.type.tensor_type.shape.dim:
        if dim.dim_value:
            dims.append(dim.dim_value)
        else:
            dims.append(dim.dim_param)
    return dims


def rewrite_qdq_boundary(
    src_path: Path,
    dst_path: Path,
    boundary_input_type: str,
    boundary_output_type: str,
) -> None:
    model = onnx.load(src_path)
    input_name = model.graph.input[0].name
    input_shape = _tensor_shape(model.graph.input[0])
    output_name = model.graph.output[0].name
    output_shape = _tensor_shape(model.graph.output[0])
    replacement_input_name = None
    replacement_output_name = None

    rewritten_nodes = []
    for node in model.graph.node:
        if (replacement_input_name is None) and (node.op_type == "QuantizeLinear") and (node.input[0] == input_name):
            replacement_input_name = node.output[0]
            continue
        if (replacement_output_name is None) and (node.op_type == "DequantizeLinear") and (node.output[0] == output_name):
            replacement_output_name = node.input[0]
            continue
        rewritten_nodes.append(node)

    if replacement_input_name is None:
        raise RuntimeError(f"Could not find boundary QuantizeLinear for input '{input_name}' in {src_path}")
    if replacement_output_name is None:
        raise RuntimeError(f"Could not find boundary DequantizeLinear for output '{output_name}' in {src_path}")

    del model.graph.node[:]
    model.graph.node.extend(rewritten_nodes)

    del model.graph.input[:]
    model.graph.input.extend(
        [helper.make_tensor_value_info(replacement_input_name, _TENSOR_TYPES[boundary_input_type], input_shape)]
    )

    del model.graph.output[:]
    model.graph.output.extend(
        [helper.make_tensor_value_info(replacement_output_name, _TENSOR_TYPES[boundary_output_type], output_shape)]
    )

    kept_value_info = []
    for value_info in model.graph.value_info:
        if value_info.name in {input_name, output_name}:
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
    parser.add_argument("--boundary-input-type", choices=sorted(_TENSOR_TYPES), default="int8",
                        help="Tensor type to expose on the rewritten model input")
    parser.add_argument("--boundary-output-type", choices=sorted(_TENSOR_TYPES), default="int8",
                        help="Tensor type to expose on the rewritten model output")
    args = parser.parse_args()

    input_model = Path(args.input_model).resolve()
    calibration_dir = Path(args.calibration_dir).resolve()
    qdq_output = Path(args.qdq_output).resolve()
    local_tmp = qdq_output.parent / "ort_tmp_local"
    local_tmp.mkdir(parents=True, exist_ok=True)
    os.environ["TMP"] = str(local_tmp)
    os.environ["TEMP"] = str(local_tmp)
    os.environ["TMPDIR"] = str(local_tmp)
    tempfile.tempdir = str(local_tmp)

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
        rewrite_qdq_boundary(
            qdq_output,
            Path(args.int8_boundary_output).resolve(),
            boundary_input_type=args.boundary_input_type,
            boundary_output_type=args.boundary_output_type,
        )

    print(qdq_output)


if __name__ == "__main__":
    main()
