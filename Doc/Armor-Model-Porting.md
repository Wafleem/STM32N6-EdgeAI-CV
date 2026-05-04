# Armor Model Porting

This repo is now staged around the Jetson-side regular detect model copied into:

- `Model/armor_yolo11n_detect_640.onnx`

## Why this model

This ONNX came from the Jetson repo path `yolov8_ros/models/best.onnx`.

Its matching TensorRT engine metadata indicates:

- YOLO11n
- `task: detect`
- `imgsz: [640, 640]`
- `names: {"0": "armor"}`

That makes it the best fit for the STM32N6 object-detection app because the app already supports YOLOv8-style detection post-processing, while the OBB models in the Jetson repo would require custom post-processing.

## Generation helpers

Board-specific generation scripts were added:

- `Model/generate-n6-model_NUCLEO-N657X0-Q_armor-detect.sh`

They follow the same flow as the stock model scripts:

1. Compile the ONNX with `stedgeai`
2. Copy generated model sources into `Model/<board>/`
3. Convert `network_atonbuf.xSPI2.raw` into `network_data.hex`

## Expected app configuration

Once `stedgeai generate` succeeds, update the active board `app_config.h` to use YOLOv8 object detection.

- `Application/NUCLEO-N657X0-Q/Inc/app_config.h`

Expected starting values:

```c
#define POSTPROCESS_TYPE POSTPROCESS_OD_YOLO_V8_UI

#define NB_CLASSES 1
#define CLASSES_TABLE const char* classes_table[NB_CLASSES] = {\
"armor"}

#define AI_OD_YOLOV8_PP_TOTAL_BOXES       (8400)
#define AI_OD_YOLOV8_PP_NB_CLASSES        (1)

#define AI_OD_YOLOV8_PP_CONF_THRESHOLD    (0.40f)
#define AI_OD_YOLOV8_PP_IOU_THRESHOLD     (0.50f)
#define AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT   (10)
```

## Values you still need after generation

Do not guess these two values. Pull them from the compiled model outputs after `stedgeai` generation:

- `AI_OD_YOLOV8_PP_ZERO_POINT`
- `AI_OD_YOLOV8_PP_SCALE`

Those are required for `POSTPROCESS_OD_YOLO_V8_UI`.

## Recommendation

Start with this regular detect model before trying any OBB model:

- `bestmerge.onnx` is OBB with one class `plate`
- `best-roboflow-nitish-obb.engine` is OBB with classes `blue` and `red`
- `bestObb2.engine` is OBB with armor-ID classes

All of those would need custom STM32 post-processing.
