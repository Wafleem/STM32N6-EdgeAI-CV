#!/usr/bin/env python3

import argparse
import shutil
from pathlib import Path

import onnx
from onnx import TensorProto, helper
from ultralytics import YOLO

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


def _get_shape(value_info) -> list[int | str]:
    dims = []
    for dim in value_info.type.tensor_type.shape.dim:
        dims.append(dim.dim_value if dim.dim_value else dim.dim_param)
    return dims


def _producer_map(model):
    producers = {}
    for node in model.graph.node:
        for output in node.output:
            producers[output] = node
    return producers


def _consumer_map(model):
    consumers = {}
    for node in model.graph.node:
        for input_name in node.input:
            if input_name:
                consumers.setdefault(input_name, []).append(node)
    return consumers


def _find_node(model, op_type: str, name_suffix: str):
    for node in model.graph.node:
        if node.op_type == op_type and node.name.endswith(name_suffix):
            return node
    raise RuntimeError(f"Could not find ONNX node {op_type} ending with {name_suffix!r}")


def _attr_int(node, name: str, default: int) -> int:
    for attr in node.attribute:
        if attr.name == name:
            return attr.i
    return default


def _prune_unreachable_nodes(model, output_names: list[str]) -> None:
    producers = _producer_map(model)
    required_values = set(output_names)
    required_nodes = []
    pending = list(output_names)

    while pending:
        value = pending.pop()
        node = producers.get(value)
        if node is None or node in required_nodes:
            continue

        required_nodes.append(node)
        for input_name in node.input:
            if input_name and input_name not in required_values:
                required_values.add(input_name)
                pending.append(input_name)

    required_node_ids = {id(node) for node in required_nodes}
    kept_nodes = [node for node in model.graph.node if id(node) in required_node_ids]
    del model.graph.node[:]
    model.graph.node.extend(kept_nodes)

    kept_initializers = [initializer for initializer in model.graph.initializer if initializer.name in required_values]
    del model.graph.initializer[:]
    model.graph.initializer.extend(kept_initializers)

    kept_value_info = [value_info for value_info in model.graph.value_info if value_info.name in required_values]
    del model.graph.value_info[:]
    model.graph.value_info.extend(kept_value_info)


def rewrite_obb_output_to_raw_head(path: Path) -> None:
    """Replace Ultralytics' decoded OBB output with raw head logits.

    ST recommends keeping object-detection postprocessing outside the Neural-ART
    graph. Ultralytics OBB export does not add NMS by default, but it does append
    a decode tail: DFL softmax, anchor math, trig, and final OBB concat. This
    rewires the exported graph to output the tensors needed for MCU-side decode:
    64 DFL box channels, class logits, and one raw angle-logit channel.
    """
    model = onnx.load(path)
    model = onnx.shape_inference.infer_shapes(model)
    producers = _producer_map(model)

    if len(model.graph.output) != 1:
        raise RuntimeError(f"Expected one Ultralytics OBB output, found {len(model.graph.output)}")

    final_output = model.graph.output[0].name
    final_concat = producers.get(final_output)
    if final_concat is None or final_concat.op_type != "Concat" or len(final_concat.input) != 3:
        rewrite_obb_output_to_raw_yolo26_head(model, producers, final_output, path)
        return

    class_sigmoid = producers.get(final_concat.input[1])
    if class_sigmoid is None or class_sigmoid.op_type != "Sigmoid":
        raise RuntimeError("Expected decoded OBB output channel 1 to come from class Sigmoid")
    raw_class = class_sigmoid.input[0]

    angle_mul = producers.get(final_concat.input[2])
    angle_sub = producers.get(angle_mul.input[0]) if angle_mul is not None and angle_mul.op_type == "Mul" else None
    angle_sigmoid = producers.get(angle_sub.input[0]) if angle_sub is not None and angle_sub.op_type == "Sub" else None
    if angle_sigmoid is None or angle_sigmoid.op_type != "Sigmoid":
        raise RuntimeError("Expected decoded OBB angle to come from Sigmoid/Sub/Mul angle decode")
    raw_angle = angle_sigmoid.input[0]

    dfl_reshape = _find_node(model, "Reshape", "/dfl/Reshape")
    raw_box = dfl_reshape.input[0]

    output_shape = _get_shape(model.graph.output[0])
    if len(output_shape) != 3 or output_shape[1] < 6:
        raise RuntimeError(f"Unexpected decoded OBB output shape: {output_shape}")
    batch = output_shape[0]
    total_boxes = output_shape[2]
    nb_classes = int(output_shape[1]) - 5
    raw_channels = 64 + nb_classes + 1
    raw_output = "raw_obb_head"

    raw_concat = helper.make_node(
        "Concat",
        inputs=[raw_box, raw_class, raw_angle],
        outputs=[raw_output],
        name="/model.23/raw_obb_head",
        axis=_attr_int(final_concat, "axis", 1),
    )
    model.graph.node.append(raw_concat)

    del model.graph.output[:]
    model.graph.output.extend(
        [helper.make_tensor_value_info(raw_output, TensorProto.FLOAT, [batch, raw_channels, total_boxes])]
    )
    _prune_unreachable_nodes(model, [raw_output])

    onnx.checker.check_model(model)
    onnx.save(model, path)


def rewrite_obb_output_to_raw_yolo26_head(model, producers, final_output: str, path: Path) -> None:
    """Cut newer YOLO OBB exports back to raw per-grid predictions.

    Some newer Ultralytics OBB models export a dense decoded tensor and then add
    a TopK candidate-selection tail even when NMS is disabled. This removes both
    the selection tail and the bbox decode math, leaving [ltrb raw box,
    class logits..., angle] for MCU-side decode, thresholding, and NMS.
    """
    final_node = producers.get(final_output)
    if final_node is None:
        raise RuntimeError("Could not find ONNX producer for final OBB output")

    shape_model = onnx.shape_inference.infer_shapes(model)
    shapes = {
        value_info.name: _get_shape(value_info)
        for value_info in list(shape_model.graph.value_info) + list(shape_model.graph.output)
    }

    pending = list(final_node.input)
    visited = set()
    dense_output = None

    while pending:
        value_name = pending.pop()
        if value_name in visited:
            continue
        visited.add(value_name)

        node = producers.get(value_name)
        if node is None:
            continue

        if node.op_type == "Transpose":
            input_shape = shapes.get(node.input[0])
            output_shape = shapes.get(node.output[0])
            if (
                input_shape is not None
                and output_shape is not None
                and len(input_shape) == 3
                and len(output_shape) == 3
                and int(input_shape[1]) >= 6
                and int(input_shape[2]) > 300
            ):
                dense_output = node.input[0]
                break

        pending.extend([input_name for input_name in node.input if input_name])

    if dense_output is None:
        raise RuntimeError("Could not find dense OBB tensor before TopK/selection tail")

    dense_shape = shapes.get(dense_output)
    if dense_shape is None:
        raise RuntimeError(f"Could not infer shape for dense OBB tensor {dense_output!r}")

    consumers = _consumer_map(model)
    dense_concat = producers.get(dense_output)
    if dense_concat is None or dense_concat.op_type != "Concat" or len(dense_concat.input) != 3:
        raise RuntimeError("Expected dense OBB tensor to be produced by a 3-input Concat node")

    decoded_boxes = dense_concat.input[0]
    class_scores = dense_concat.input[1]
    raw_angle = dense_concat.input[2]

    class_sigmoid = producers.get(class_scores)
    if class_sigmoid is None or class_sigmoid.op_type != "Sigmoid":
        raise RuntimeError("Expected dense OBB class scores to come from Sigmoid")
    raw_class = class_sigmoid.input[0]

    raw_box = None
    for node in model.graph.node:
        output_name = node.output[0] if node.output else ""
        output_shape = shapes.get(output_name)
        if (
            node.op_type == "Concat"
            and output_shape is not None
            and len(output_shape) == 3
            and int(output_shape[1]) == 4
            and output_shape[2] == dense_shape[2]
            and any(consumer.op_type == "Split" for consumer in consumers.get(output_name, []))
        ):
            raw_box = output_name
            break

    if raw_box is None:
        raise RuntimeError("Could not find raw YOLO OBB box tensor before decode")

    raw_output = "raw_obb_head"
    raw_channels = int(dense_shape[1])
    del model.graph.output[:]
    model.graph.output.extend(
        [helper.make_tensor_value_info(raw_output, TensorProto.FLOAT, [dense_shape[0], raw_channels, dense_shape[2]])]
    )

    raw_concat = helper.make_node(
        "Concat",
        inputs=[raw_box, raw_class, raw_angle],
        outputs=[raw_output],
        name="/model.23/raw_obb_head",
        axis=1,
    )
    model.graph.node.append(raw_concat)
    _prune_unreachable_nodes(model, [raw_output])

    onnx.checker.check_model(model)
    onnx.save(model, path)


def verify_raw_obb_head(path: Path, expected_classes: int | None = None, expected_boxes: int | None = None) -> None:
    model = onnx.load(path)
    output_shape = _get_shape(model.graph.output[0])
    op_counts = {}
    for node in model.graph.node:
        op_counts[node.op_type] = op_counts.get(node.op_type, 0) + 1

    forbidden_ops = {op: op_counts[op] for op in sorted(POSTPROCESS_OPS) if op_counts.get(op)}
    if forbidden_ops:
        raise RuntimeError(f"NPU-side postprocess ops remain in {path}: {forbidden_ops}")

    if len(output_shape) != 3 or output_shape[1] < 6 or output_shape[2] < 300:
        raise RuntimeError(f"Expected raw OBB head [batch, channels, boxes], got {output_shape}")

    if expected_classes is not None:
        expected_channels = expected_classes + 5
        if int(output_shape[1]) != expected_channels:
            raise RuntimeError(f"Expected raw OBB channels={expected_channels}, got {output_shape}")

    if expected_boxes is not None and int(output_shape[2]) != expected_boxes:
        raise RuntimeError(f"Expected raw OBB boxes={expected_boxes}, got {output_shape}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Export a Safal-style Ultralytics OBB checkpoint to ONNX.")
    parser.add_argument("checkpoint", help="Path to the source .pt checkpoint")
    parser.add_argument("output", help="Path to the output .onnx file")
    parser.add_argument("--imgsz", type=int, default=640, help="Square export size")
    parser.add_argument("--opset", type=int, default=13, help="ONNX opset")
    parser.add_argument("--decoded-output", action="store_true",
                        help="Keep Ultralytics' decoded OBB output in the ONNX graph")
    parser.add_argument("--expected-classes", type=int, help="Fail unless the raw head has classes + 5 channels")
    parser.add_argument("--expected-boxes", type=int, help="Fail unless the raw head has this candidate count")
    args = parser.parse_args()

    checkpoint = Path(args.checkpoint).resolve()
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    model = YOLO(str(checkpoint), task="obb")
    exported_path = Path(
        model.export(format="onnx", imgsz=args.imgsz, simplify=True, opset=args.opset, nms=False)
    ).resolve()

    if exported_path != output:
        shutil.copyfile(exported_path, output)

    if not args.decoded_output:
        rewrite_obb_output_to_raw_head(output)
        verify_raw_obb_head(output, args.expected_classes, args.expected_boxes)

    print(output)


if __name__ == "__main__":
    main()
