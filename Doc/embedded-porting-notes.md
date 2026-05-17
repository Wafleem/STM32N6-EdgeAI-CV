# Embedded Porting Notes

This document describes the current RoboMaster armor-plate detector running on the `NUCLEO-N657X0-Q` STM32N6 project, plus the practical integration lessons learned while getting it working live over USB/UVC.

For narrative design-review notes, see [Design Review Notes](design-review-notes.md). For chronological trial-and-error notes, see [Bring-Up Log](bringup-log.md).

## Current Known-Good Profile

The current working live profile is:

- build profile: `SafalObb`
- display mode: USB/UVC
- board: `NUCLEO-N657X0-Q`
- camera: Sony `IMX219` Raspberry Pi style module
- model name: `BestMerge OBB 320`
- active ONNX: `Model/bestmerge_320_robomaster_v4_clean_qdq.onnx`
- generated STM32 artifacts: `Model/NUCLEO-N657X0-Q_SafalObb`
- input tensor: `uint8(1x3x320x320)`
- output tensor: `int8(1x6x2100)`
- class table: `plate`
- postprocess: custom MCU-side raw YOLO OBB decode plus rotated NMS
- display output: axis-aligned green rectangles around detected plates

The model is compiled for boot-from-flash operation. The flash flow programs:

- FSBL
- external flash model blob at `0x70380000`
- signed app image at `0x70100000`

## What Was Broken

The first successful flashes proved that the NPU was running, but detections were wrong or visually misleading:

- Inference completed, but there were either no detections or many low-confidence false positives.
- Labels appeared in the black unused part of the UVC frame.
- Boxes appeared outside the camera image.
- Some detections existed in serial output but were not drawn.
- Generic tiny YOLOv2 looked more stable, which made the custom model path look suspicious.

The important lesson: the model was not the only thing that had to be correct. The camera format, NN input layout, postprocess decode, output grid count, display coordinate system, and debug visibility all had to agree.

## Root Causes

### Channel Layout Mismatch

The IMX219/DCMIPP camera pipe provides interleaved RGB pixels, effectively:

```text
RGBRGBRGB...
```

The active BestMerge model expects channel-first input:

```text
RRRR... GGGG... BBBB...
```

The generic ST model did not expose this bug because its path could consume the camera bytes more directly. For the Safal/BestMerge model, feeding HWC camera data into a CHW model scrambled the input image from the model's point of view.

The fix was to add a preprocessing step that transposes camera HWC data into the CHW NN input buffer before `stai_network_run`.

### Output Shape And Anchor/Grid Count

The 320 model produces:

```text
1 x 6 x 2100
```

The `2100` value is not arbitrary. It is the total number of YOLO grid points/anchors for this export. If the app's postprocessor expects the wrong number of boxes, it reads the output tensor with the wrong structure. That creates classic symptoms: bad confidence, boxes in strange places, or no detections.

The current config uses:

```c
#define AI_OD_OBB_PP_TOTAL_BOXES (2100)
```

### Raw YOLO OBB Decode

This model exports a raw YOLO OBB-style tensor, not a fully postprocessed detection list. The MCU must decode:

- box center
- box size
- class confidence
- angle/rotation signal
- NMS and candidate filtering

The current display path converts the rotated result to an axis-aligned rectangle because the existing UVC overlay path is rectangle-oriented.

### Display Layer Geometry

The UVC output is `320x240`, but the live camera image is cropped to a centered `240x240` region:

```text
fg=240x240@40,0
bg=240x240@40,0
```

The foreground overlay must match the background camera layer. When the overlay was expanded to the full `320x240` screen, boxes and text were drawn relative to the wrong coordinate space and appeared in black regions outside the camera image.

The fix was to keep foreground and background aligned and draw boxes relative to the foreground layer.

### Debug Noise

Early logs printed too much low-level polling output. That made it difficult to see the actual failure signal.

The current useful debug lines are:

```text
TRACE: NN input: frame=... pitch=960 checksum64=... output0_len=12600
TRACE: OBB postprocess: run=... threshold=0.300 max_conf=... candidates=... detections=...
TRACE: display: frame=... raw=... drawn=... fg=240x240@40,0 bg=240x240@40,0
TRACE: detect: frame=... count=... box0[class=0 conf=... cx=... cy=... w=... h=...]
TRACE: perf: frame=... loop=... headless_est=... nn=... compute=... cam_wait=...
```

## Current Performance Interpretation

The serial performance line separates several different concepts:

```text
TRACE: perf: frame=1050 det=2 loop=67ms/14.9fps headless_est=58ms/17.2fps nn=39ms/25.6fps compute=44ms/22.7fps prep=2 post=3 uvc_display=9 cam_wait=14
```

Meaning:

- `nn`: neural network inference only. This is the NPU model execution cost.
- `compute`: preprocessing + NN inference + postprocess. This is the useful detection compute path.
- `loop`: full visible UVC/debug loop, including display drawing and camera wait.
- `headless_est`: approximate loop time if UVC display drawing were removed.
- `prep`: camera-to-NN preprocessing, including HWC-to-CHW layout conversion.
- `post`: OBB decode, thresholding, and NMS.
- `uvc_display`: cost of drawing boxes/text and preparing the visible UVC frame.
- `cam_wait`: how long the loop had to wait for the next camera frame after overlap.

Observed behavior at 320x320:

- NN-only inference is commonly around `25 FPS`, sometimes higher depending on measured frame.
- Preprocess and postprocess add only a few milliseconds.
- UVC/display/debug overhead costs around `8-10 ms`.
- Camera wait can range from `0 ms` to the low teens depending on frame timing.
- Headless operation should be closer to `headless_est` than to the visible UVC loop.

## Camera/Inference Overlap

The app now primes one NN camera frame, preprocesses it into `nn_in`, starts capture of the next frame, then runs inference on the current frame while the next frame is being captured.

Simplified loop:

```text
capture frame 0

loop:
  preprocess completed capture into nn_in
  start capture of next frame
  run NPU inference on nn_in
  postprocess detections
  draw/debug output
  wait only if next capture is not done yet
```

When `cam_wait=0`, the capture was fully hidden behind inference/display work. When `cam_wait` is nonzero, the CPU reached the end of the loop before the next camera frame was ready.

## Memory Pressure

The 320 build is close to the AXISRAM limit:

```text
AXISRAM1_S: ~1.676 MB / 1647 KB = 99.36%
```

AXISRAM is the fast internal RAM region used by this firmware for:

- application code/data loaded for execution
- stacks and globals
- camera capture buffer
- NN input/output and runtime state
- display/foreground buffers
- postprocess buffers

The overlap optimization adds a separate camera capture buffer so the camera can fill one buffer while the NPU consumes `nn_in`. That improves timing but increases RAM pressure. Because the current build has only about `10-11 KB` free, new debug strings, large arrays, or extra frame buffers can push the build over the limit.

## Current Tuning

The current postprocess tuning is:

```c
#define AI_OD_OBB_PP_CONF_THRESHOLD      (0.30f)
#define AI_OD_OBB_PP_IOU_THRESHOLD       (0.40f)
#define AI_OD_OBB_PP_CANDIDATES_LIMIT    (24)
#define AI_OD_OBB_PP_MAX_BOXES_LIMIT     (2)
```

This intentionally favors a small number of visible plate candidates rather than flooding the UVC view with every weak proposal.

If detections become noisy again, tune in this order:

1. Confirm `TRACE: NN input` still shows `pitch=960` and output length `12600`.
2. Confirm `fg=240x240@40,0` and `bg=240x240@40,0` still match.
3. Print box dimensions with more precision before assuming the model is wrong.
4. Raise `AI_OD_OBB_PP_CONF_THRESHOLD` if false positives are common.
5. Lower the threshold only if known plates are consistently missed.
6. Revisit calibration data only after the integration signals above look correct.

## Useful Commands

Build:

```powershell
.\scripts\stm32n6.ps1 -Action build -ModelProfile SafalObb -Jobs 2
```

Flash:

```powershell
.\scripts\stm32n6.ps1 -Action flash-all -ModelProfile SafalObb -Jobs 2
```

Generate STM32 artifacts from the active 320 ONNX:

```powershell
cd Model
stedgeai generate `
  --model bestmerge_320_robomaster_v4_clean_qdq.onnx `
  --target stm32n6 `
  --st-neural-art default@user_neuralart_NUCLEO-N657X0-Q.json `
  --input-data-type uint8 `
  --output-data-type int8 `
  --verbosity 1
```

After generation, copy the generated `network.c`, `stai_network.c`, `stai_network.h`, `network_ecblobs.h`, and binary weight blob into `Model/NUCLEO-N657X0-Q_SafalObb`.

## Future Improvements

- Print detection box coordinates in pixels as well as normalized coordinates.
- Add optional temporal holding/filtering so brief one-frame misses do not flicker targeting output.
- Benchmark headless mode without UVC drawing enabled.
- Evaluate a smaller or more NPU-friendly architecture to reduce the `nn` time below the current 320 model cost.
- Recalibrate with real camera captures from the final deployment setup.
- Consider a rotated-box display path if angle visualization becomes useful.
