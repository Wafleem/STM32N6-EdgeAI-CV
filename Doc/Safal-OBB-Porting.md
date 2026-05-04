# Safal OBB Porting Notes

This repo has a Nucleo-only `SafalObb` profile for a RoboMaster armor-plate OBB detector.

## Model Choice

Safal's Jetson launch path uses the Ultralytics OBB model `model_test/best-roboflow-nitish-obb.engine`, selected by the default `model_path` in the CV repo's `yolov8_node.py`.

The exact source artifacts for that model are present on the CV repo's `origin/Nitish` branch:

- `model_test/best-roboflow-nitish-obb.pt`
- `model_test/best-roboflow-nitish-obb.onnx`
- `model_test/best-roboflow-nitish-obb.engine`

The deployed Jetson engine metadata says:

- task: `obb`
- input size: `640x640`
- classes: `blue`, `red`
- TensorRT export: int8, batch 1, no NMS

This STM32 profile uses the same Nitish checkpoint, exported at `384x384` so it fits the NUCLEO-N657X0-Q memory pools.

## Why 384x384

The original `640x640` QDQ ONNX was quantized and tested with ST Edge AI, but Neural-ART could not allocate it for the Nucleo memory pools. The compiler reported roughly `15-16 MiB` left unallocated.

The `416x416` export and QDQ quantization also succeeded, but ST Edge AI could not allocate it for the configured Neural-ART memory pools. The compiler reported `1,730,560` bytes left unallocated, so `480x480` was not tested because it would be larger.

The `384x384` export compiles successfully and is the active profile:

- input: `uint8(1x3x384x384)`
- output: `int8(1x7x3024)`
- weights: about `2.54 MiB`
- activations: about `1.81 MiB`
- total Neural-ART memory: about `4.36 MiB`

This means benchmarking should be described as same checkpoint/model lineage, reduced Nucleo input size. For a fair Jetson-vs-STM32 comparison, run Jetson with the same `384x384` exported ONNX/checkpoint configuration.

## Active STM32 Profile

`SafalObb` is configured in [app_config.h](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Application/NUCLEO-N657X0-Q/Inc/app_config.h:1):

- `POSTPROCESS_TYPE = POSTPROCESS_CUSTOM`
- `NB_CLASSES = 2`
- classes: `blue`, `red`
- `AI_OD_OBB_PP_TOTAL_BOXES = 3024`
- aspect ratio mode: `ASPECT_RATIO_CROP`

The custom postprocessor in [app_postprocess_template.c](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Middlewares/ai-postprocessing-wrapper/app_postprocess_template.c:1) decodes Ultralytics OBB output as:

- `cx, cy, w, h`
- per-class confidence channels
- final angle channel

It converts the rotated box corners into an axis-aligned rectangle for the existing display and targeting path.

## Reproducing Artifacts

Export the Nitish checkpoint at 384:

```powershell
python Model\export-ultralytics-obb-to-onnx.py `
  Model\nitish_red_blue_obb.pt `
  Model\nitish_red_blue_obb_384.onnx `
  --imgsz 384
```

Prepare calibration tensors:

```powershell
python Model\prepare-calibration-npy.py `
  Model\calibration_images\nyu_robomasters_v6 `
  Model\calibration_npy\nitish_384 `
  --imgsz 384 `
  --max-images 192
```

Quantize the ONNX:

```powershell
python Model\quantize-obb-onnx.py `
  Model\nitish_red_blue_obb_384.onnx `
  Model\calibration_npy\nitish_384 `
  Model\nitish_red_blue_obb_384_qdq.onnx
```

The export step needs `torch` and `ultralytics`. The quantization step needs `onnx`, `onnxruntime`, `numpy`, and `sympy`.

Generate Nucleo artifacts:

```powershell
cd Model
bash ./generate-n6-model_NUCLEO-N657X0-Q_safal-obb.sh nitish_red_blue_obb_384_qdq.onnx
```

Build the app:

```powershell
.\build.ps1 -Board NUCLEO-N657X0-Q -ModelProfile SafalObb -Jobs 2
```

## Benchmark Caveats

The real Safal Jetson launch points to the Nitish `640x640` TensorRT engine. The STM32 build uses the same checkpoint exported at `384x384`.

That is a good embedded benchmark target, but not identical to the deployed Jetson engine unless Jetson is also run at `384x384` from the same checkpoint or ONNX.
