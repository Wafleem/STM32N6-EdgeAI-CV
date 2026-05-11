# Safal OBB Porting Log

This is the working log for porting a RoboMaster armor-plate detector from the Jetson CV repo into this STM32N6 Nucleo project.

## Goal

The target is a buildable `SafalObb` profile that can run a RoboMaster armor-plate detector on the `NUCLEO-N657X0-Q`, while staying close enough to the Jetson model lineage to support later Jetson-vs-STM32 benchmarking.

The current active result is:

- live tested profile: `BestMerge OBB 320` through `SafalObb`
- exported/quantized model currently compiled into the app: `Model/bestmerge_320_robomaster_v4_clean_qdq.onnx`
- generated STM32 artifacts: `Model/NUCLEO-N657X0-Q_SafalObb`
- model profile: `SafalObb`
- class: `plate`
- input tensor: `uint8(1x3x320x320)`, channel-first
- output tensor: `int8(1x6x2100)`
- display status: UVC overlay draws stable boxes around armor plates after the May 2026 live-camera fixes below

Older Nitish/Safal 384 notes are kept later in this log for lineage and reproduction context:

- source checkpoint: `Model/nitish_red_blue_obb.pt`
- exported ONNX: `Model/nitish_red_blue_obb_384.onnx`
- quantized ONNX: `Model/nitish_red_blue_obb_384_qdq.onnx`
- generated STM32 artifacts: `Model/NUCLEO-N657X0-Q_SafalObb`
- model profile: `SafalObb`
- classes: `blue`, `red`
- input tensor: `uint8(1x3x384x384)`
- output tensor: `int8(1x7x3024)`

## 2026-05-10 Live UVC Detection Fix

The first BestMerge-on-board flashes proved the NPU was running, but the live UVC result was wrong:

- Serial showed inference completing consistently around `94-115 ms`.
- Postprocess sometimes produced many low-confidence `plate` detections, then later reported `detections=0` while stale boxes still appeared in the black area.
- Labels and boxes were drawn outside the camera image, mostly in the black unused part of the UVC frame.
- The model did not initially draw boxes around the armor plate images shown to the camera, even though AI Runner validation looked healthy.

The root causes were in the application integration, not in the trained model:

- The BestMerge model input is channel-first `1x3x320x320`, while the camera pipe writes interleaved RGB pixels. The generic model is channel-last, so direct camera bytes worked there but scrambled this model's input.
- The foreground overlay was temporarily expanded to the full UVC screen. The screen compositor expects both layers to have the same origin and size, so that made boxes render in black regions instead of staying on the camera layer.
- The display path was drawing labels for every noisy candidate, which made it hard to tell whether the real plate was detected.
- The debug output printed every inference poll plus UVC internals, which hid the few lines that actually mattered for this failure.

The working fix:

- Add a camera-to-NN preprocessing path that detects channel-first input from `STAI_NETWORK_IN_1_FLAGS` and transposes camera HWC bytes into CHW before inference.
- Keep the display background and foreground layers the same size and origin, then draw box coordinates relative to the foreground layer.
- Clear both foreground overlay buffers before drawing each frame so stale boxes cannot persist.
- Remove per-box text labels during live bring-up and show only thick green rectangles plus the object count.
- Raise and cap postprocess output for debugging: threshold `0.30`, IoU `0.40`, candidate limit `24`, max boxes `2`.
- Reduce serial logs to startup configuration, periodic NN input checksum/sample lines, periodic OBB postprocess summaries, frame summaries, and display summaries.

Useful known-good trace lines after this fix:

```text
TRACE: NN init: input=320x320x3 ... flags=... out0_bytes=12600 ...
Display: bg=240x240@40,0 fg=240x240@40,0 nn_pitch=960 color_swap=1 chw=1
TRACE: NN input: frame=... pitch=960 align=... checksum64=... sample=[...]
TRACE: OBB postprocess: run=... threshold=0.300 max_conf=... candidates=... detections=...
TRACE: display: frame=... raw=... drawn=... fg=240x240@40,0 bg=240x240@40,0
```

If boxes ever return to the black area, first check that foreground and background sizes/origins match in the `Display:` and `TRACE: display:` lines. If detection confidence collapses again, check the `chw=1`, `pitch=960`, and `TRACE: NN input` lines before retraining or recalibrating.

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
