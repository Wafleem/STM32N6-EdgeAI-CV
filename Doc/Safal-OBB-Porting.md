# Safal OBB Porting Notes

This repo is now prepared for a custom Ultralytics OBB deployment path on STM32N6.

The key point is that Safal's Jetson launch path does **not** use a standard YOLO detect model. It launches `yolov8_modular_node` without a model override in [stm.launch.safal.py](</C:/Users/saysa/Documents/Robomaster_CodeStuff/cv_detection/CV_Detection/yolov8_ros/launch/stm.launch.safal.py:214>), and the default model comes from [yolov8_node.py](</C:/Users/saysa/Documents/Robomaster_CodeStuff/cv_detection/CV_Detection/yolov8_ros/yolov8_ros/yolov8_node.py:301>). That default is `model_test/best-roboflow-nitish-obb.engine` with `task="obb"` in [yolov8_node.py](</C:/Users/saysa/Documents/Robomaster_CodeStuff/cv_detection/CV_Detection/yolov8_ros/yolov8_ros/yolov8_node.py:309>) and [yolov8_node.py](</C:/Users/saysa/Documents/Robomaster_CodeStuff/cv_detection/CV_Detection/yolov8_ros/yolov8_ros/yolov8_node.py:329>).

## What Was Added Here

- `POSTPROCESS_CUSTOM` is now enabled in both board configs.
- A custom OBB postprocess decoder lives in [app_postprocess_template.c](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Middlewares/ai-postprocessing-wrapper/app_postprocess_template.c:1).
- The decoder expects an exported Ultralytics OBB inference tensor in the standard exported format:
  - decoded `cx, cy, w, h`
  - per-class confidences
  - final angle channel
- The decoder converts rotated boxes into axis-aligned rectangles so the existing STM32 ROI drawing path still works.
- Both board apps now accept the custom postprocess parameter type in:
  - [NUCLEO main.c](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Application/NUCLEO-N657X0-Q/Src/main.c:89)
  - [DK main.c](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Application/STM32N6570-DK/Src/main.c:80)

## Benchmark-Accurate Source Model

The exact source ONNX or `.pt` for `best-roboflow-nitish-obb.engine` is not present in the CV repo, so we cannot regenerate that exact TensorRT engine from source here.

The closest source checkpoint I found to Safal's deployed Jetson engine is:

- [best_yolo_obb_shubham.pt](</C:/Users/saysa/Documents/Robomaster_CodeStuff/cv_detection/CV_Detection/yolov8_ros/models/best_yolo_obb_shubham.pt>)

Why this checkpoint matters:

- it is an Ultralytics OBB checkpoint
- it embeds `blue-armor` and `red-armor` class strings
- that is much closer to Safal's deployed red/blue OBB engine than the available one-class or `B1..R7` source models

## What You Still Need To Do

1. Export the Jetson-style OBB checkpoint to ONNX.

Use [export-ultralytics-obb-to-onnx.py](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Model/export-ultralytics-obb-to-onnx.py:1) in an environment that has `torch` and `ultralytics` installed:

```bash
python Model/export-ultralytics-obb-to-onnx.py \
  "C:/Users/saysa/Documents/Robomaster_CodeStuff/cv_detection/CV_Detection/yolov8_ros/models/best_yolo_obb_shubham.pt" \
  "C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Model/safal_red_blue_obb.onnx" \
  --imgsz 640
```

2. Generate the STM32 model artifacts with STEdgeAI.

For NUCLEO:

```bash
cd Model
./generate-n6-model_NUCLEO-N657X0-Q_safal-obb.sh safal_red_blue_obb.onnx
```

For DK:

```bash
cd Model
./generate-n6-model_STM32N6570-DK_safal-obb.sh safal_red_blue_obb.onnx
```

3. Rebuild and flash the full project, not `AppOnly`.

The model weights live in `Model/<board>/network_data.hex`, so a model swap requires a full model regenerate and a full flash.

## Current STM32 Assumptions

Both board configs are currently set up for the intended Safal-style benchmark target:

- `NB_CLASSES = 2`
- class names are `blue`, `red`
- expected Ultralytics OBB input size is effectively `640x640`
- aspect ratio mode is crop-based to stay closer to the Jetson square ROI path

If your exported ONNX does **not** have exactly 2 classes, `app_postprocess_init()` will fail intentionally. That is deliberate because this port is meant to stay faithful to the red/blue benchmark target.

## Important Caveat About Fairness

This repo is prepared for the same **family** of OBB model behavior as Safal's Jetson path, but exact benchmark parity still depends on using an ONNX exported from the matching red/blue OBB checkpoint. Do not use the available `bestObb2.onnx` or `bestmerge.onnx` if your goal is a fair Jetson-vs-STM32 benchmark of the model Safal actually runs.
