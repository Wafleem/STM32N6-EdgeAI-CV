# Safal OBB Porting Log

This is the working log for porting a RoboMaster armor-plate detector from the Jetson CV repo into this STM32N6 Nucleo project.

## Goal

The target is a buildable `SafalObb` profile that can run a RoboMaster armor-plate detector on the `NUCLEO-N657X0-Q`, while staying close enough to the Jetson model lineage to support later Jetson-vs-STM32 benchmarking.

The current active result is:

- source checkpoint: `Model/nitish_red_blue_obb.pt`
- exported ONNX: `Model/nitish_red_blue_obb_384.onnx`
- quantized ONNX: `Model/nitish_red_blue_obb_384_qdq.onnx`
- generated STM32 artifacts: `Model/NUCLEO-N657X0-Q_SafalObb`
- model profile: `SafalObb`
- classes: `blue`, `red`
- input tensor: `uint8(1x3x384x384)`
- output tensor: `int8(1x7x3024)`

## Model Search

The external CV repo had multiple YOLO/OBB candidates. The important distinction was whether we wanted any usable RoboMaster detector or the model implied by Safal's Jetson launch path.

Candidates examined:

- `bestmerge`: a RoboMaster plate detector with `.pt`, `.onnx`, and `.engine` style artifacts available under model-test areas. It looked usable for plate-only detection, but it was not the model pointed to by the Safal STM32/Jetson launch path.
- Shubham OBB model: source checkpoint was available and the class set looked useful for RoboMaster detection. It was a good fallback because it could be exported and transformed cleanly.
- Nitish/Safal OBB model: the Jetson launch script used `model_test/best-roboflow-nitish-obb.engine` by default. That made it the best choice for fair lineage. The matching `.pt`, `.onnx`, and `.engine` artifacts were found on the CV repo's `origin/Nitish` branch.

Final model choice:

- Use the Nitish/Safal checkpoint because it is the closest match to the model actually implied by the Jetson launch script.
- Keep the Jetson reference in mind as `640x640` TensorRT INT8 OBB, but export a smaller STM32 version that fits the Nucleo memory pools.

## Dataset And Calibration

Real deployment images would be best for quantization calibration. Since those were not available yet, the temporary calibration set came from the local RoboFlow export:

- `C:\Users\saysa\Downloads\Robomasters.v6i.yolov8`

The calibration images should represent what the camera will see during matches:

- real armor plates in frame, not just clean crops
- red and blue plates
- different ranges, angles, exposure levels, and motion blur
- backgrounds similar to the field
- enough negative/background frames to avoid overconfident false positives

The helper script turns images into `.npy` tensors for ONNX Runtime static QDQ quantization.

## Porting Work

This repo now has compile-time model profile selection:

- `Generic`: upstream tiny YOLOv2 sample model
- `SafalObb`: RoboMaster Nitish OBB model

The Nucleo application selects the profile with `APP_MODEL_PROFILE`. The Windows build helper exposes this as:

```powershell
.\build.ps1 -Board NUCLEO-N657X0-Q -ModelProfile Generic
.\build.ps1 -Board NUCLEO-N657X0-Q -ModelProfile SafalObb
```

The generated model artifacts are separated by profile:

- `Model/NUCLEO-N657X0-Q`
- `Model/NUCLEO-N657X0-Q_SafalObb`

The custom postprocessor decodes Ultralytics OBB output channels:

- `cx`, `cy`, `w`, `h`
- class confidence channels
- final angle channel

For now, the rotated OBB is converted to an axis-aligned box so it can reuse the existing display and targeting path. That means the screen should show normal rectangular boxes labeled `blue` or `red`, not rotated polygons.

## Resolution Experiments

Several input sizes were tested because memory fit was the main blocker.

`640x640`:

- This is the Jetson reference input size.
- Export and QDQ quantization were possible.
- ST Edge AI Neural-ART allocation failed with roughly `15-16 MiB` left unallocated.
- Verdict: too large for the current Nucleo memory configuration.

`320x320`:

- Export, QDQ quantization, ST Edge AI generation, and full firmware build succeeded.
- Output shape was `1x7x2100`.
- This was the first safe fallback.
- Verdict: works, but lower resolution than necessary.

`416x416`:

- Export and QDQ quantization succeeded.
- ST Edge AI failed Neural-ART allocation with `1,730,560` bytes left unallocated.
- Verdict: does not fit.

`480x480`:

- Not tested after `416x416` failed.
- Since it is larger than `416x416`, it is expected not to fit without deeper memory or model changes.
- Verdict: not a practical next step right now.

`384x384`:

- Export, QDQ quantization, ST Edge AI generation, and full firmware build succeeded.
- Output shape is `1x7x3024`.
- ST Edge AI reported about `2.55 MiB` weights, `1.81 MiB` activations, and `4.36 MiB` total Neural-ART memory.
- Full SafalObb firmware build fit in AXISRAM1_S at about `86.75%`.
- Verdict: best current balance, and now the active profile.

## Reproduction Commands

Export:

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

Quantize:

```powershell
python Model\quantize-obb-onnx.py `
  Model\nitish_red_blue_obb_384.onnx `
  Model\calibration_npy\nitish_384 `
  Model\nitish_red_blue_obb_384_qdq.onnx
```

Generate STM32N6 artifacts with ST Edge AI:

```powershell
cd Model
stedgeai generate `
  --model nitish_red_blue_obb_384_qdq.onnx `
  --target stm32n6 `
  --st-neural-art default@user_neuralart_NUCLEO-N657X0-Q.json `
  --input-data-type uint8 `
  --output-data-type int8
```

Package the generated artifacts:

```powershell
cd Model
bash ./generate-n6-model_NUCLEO-N657X0-Q_safal-obb.sh nitish_red_blue_obb_384_qdq.onnx
```

Build:

```powershell
.\build.ps1 -Board NUCLEO-N657X0-Q -ModelProfile SafalObb -Jobs 2
```

## Benchmark Notes

The Jetson launch path points to a `640x640` TensorRT INT8 engine. The STM32 profile uses the same Nitish checkpoint exported at `384x384`.

That is still a meaningful embedded benchmark because the model lineage is the same, but it is not an exact apples-to-apples comparison unless the Jetson is also run with the same `384x384` export. The future benchmark should report both facts clearly:

- `Jetson real deployment`: Nitish TensorRT engine at `640x640`
- `STM32 deployment`: same Nitish checkpoint at `384x384`, QDQ ONNX, ST Edge AI Neural-ART artifacts

## Expected Deployment Behavior

When flashed successfully, the board should boot the `SafalObb` profile and show `Nitish Safal OBB 384` in the startup text.

The video overlay should draw axis-aligned boxes around detected armor plates with labels:

- `blue`
- `red`

The current postprocess does not draw rotated boxes. If rotated polygon visualization becomes important, the OBB corner coordinates are already computed and can be carried through a richer display path later.

## Future Work

- Flash on actual Nucleo hardware and confirm boot/video behavior.
- Tune confidence and IoU thresholds using real match footage.
- Replace temporary RoboFlow calibration images with real camera frames from the deployment setup.
- Benchmark Jetson with the same `384x384` checkpoint export if strict fairness is required.
- Consider rotated-box display or downstream angle use once the basic detector is stable.
