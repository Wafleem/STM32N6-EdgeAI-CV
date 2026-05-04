Temporary calibration image pools for ONNX/PTQ preparation live here.

Current pool:

- `nyu_robomasters_v6/`
  - Source: `C:\Users\saysa\Downloads\Robomasters.v6i.yolov8`
  - Roboflow workspace/project/version: `nyu-robomasters/robomasters-nc6sw/6`
  - License noted by dataset export: `CC BY 4.0`
  - Purpose: temporary calibration fallback until we have real Jetson camera frames
  - Sampling used here: `220` images from `train/images` and `40` from `test/images`

These images are intended for quantization/calibration only. For the fairest
Jetson-vs-STM32 benchmark, replace or supplement this pool with representative
frames captured from the actual Jetson camera pipeline.
