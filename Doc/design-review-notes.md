# Armor Plate Design Review Notes

This guide is written as a reference document for explaining the STM32N6 armor-plate detection project in interviews, design reviews, or project demos.

## One-Minute Project Summary

This project ports a RoboMaster armor-plate detector from a larger CV/Jetson-style workflow onto an STM32N6 microcontroller with an integrated NPU. The board captures video from an IMX219 camera, preprocesses the camera frame into the model's expected tensor layout, runs a quantized OBB detector through ST Edge AI/Neural-ART, decodes the raw YOLO OBB output on the MCU, and streams a debug view over USB/UVC with detection boxes and performance metrics.

The interesting engineering work was not just "run a model on an MCU." The real work was aligning the full embedded vision pipeline:

- camera sensor bring-up
- DCMIPP/CSI capture
- color and tensor layout
- quantized model generation
- raw OBB postprocessing
- display coordinate mapping
- real-time performance measurement
- RAM constraints
- camera/inference overlap

## System Architecture

High-level path:

```text
IMX219 camera
  -> CSI/DCMIPP camera pipe
  -> RGB camera frame
  -> HWC-to-CHW preprocessing
  -> uint8(1x3x320x320) NN input
  -> STM32N6 NPU via ST Edge AI runtime
  -> int8(1x6x2100) raw OBB output
  -> MCU-side YOLO OBB decode + NMS
  -> display-safe rectangles
  -> USB/UVC debug stream
```

Current working profile:

- board: `NUCLEO-N657X0-Q`
- camera: Sony `IMX219`
- model: `BestMerge OBB 320`
- input: `uint8(1x3x320x320)`
- output: `int8(1x6x2100)`
- class: `plate`
- display: `240x240` camera crop centered in a `320x240` UVC stream

## What Made This Hard

On a desktop or Jetson, frameworks hide a lot of the pipeline. On an MCU, every assumption becomes explicit:

- The model's tensor layout must match the camera preprocessing exactly.
- The postprocessor must match the exported tensor shape exactly.
- Quantization changes scales, zero-points, and output interpretation.
- Display layers have their own coordinate system.
- RAM is limited enough that one extra frame buffer matters.
- Debug output can change timing and hide the real signal if it is too noisy.

This is why the project looked broken even when the model was actually running.

## Trial And Error Timeline

### 1. Camera Bring-Up

The first job was proving the camera path worked.

Successful signs:

```text
TRACE: IMX219_ReadID: combined id=0x0219
TRACE: CMW_CAMERA_Probe_Sensor: selected IMX219
TRACE: CMW_CAMERA_Start: Camera_Drv.Start ret=0
TRACE: CameraPipeline_DisplayPipe_Start: ret=0
```

Design-review framing:

> We made the hardware pipeline observable first. Before blaming the model, we verified that the IMX219 sensor responded over I2C, the driver selected the expected mode, CSI/DCMIPP started, and UVC enumeration happened.

### 2. Model Artifact Search

The CV repo had multiple possible artifacts:

- generic/regular detection models
- OBB models
- TensorRT engines
- ONNX exports
- training checkpoints

The STM32 path needed an artifact that could be exported/quantized and whose output could be decoded on the MCU. The current working path uses the BestMerge armor-plate OBB lineage exported to 320x320.

Design-review framing:

> The deployed model format on Jetson is not automatically the right embedded artifact. TensorRT engines are not portable to STM32N6, so we needed a quantizable ONNX path and a postprocessor that matched its raw output.

### 3. Size Experiments

The natural instinct is to keep a larger input size for accuracy, but the Nucleo memory pools constrain what can run.

Resolution lessons:

- `640x640`: too large for this Nucleo deployment.
- `416x416`: still too large for the tested Neural-ART memory allocation.
- `384x384`: useful during earlier experiments, but not the final live profile.
- `288x288`: worked and helped validate the pipeline at lower memory cost.
- `320x320`: current working balance of detection quality and fit.

Design-review framing:

> The final resolution was chosen by trading accuracy, latency, and memory fit. 320x320 is large enough to detect the armor plate in the current camera view while still fitting the STM32N6 memory budget.

### 4. Anchor/Grid Output Mismatch

The active model output is:

```text
1 x 6 x 2100
```

The `2100` count describes the model's output grid/anchor positions. If the MCU postprocessor expects the wrong count, it reads the tensor incorrectly.

Symptoms of this problem:

- `detections=0` even though inference runs
- strange boxes
- unstable confidences
- false boxes in weird places

Fix:

```c
#define AI_OD_OBB_PP_TOTAL_BOXES (2100)
```

Design-review framing:

> The postprocessor is part of the model contract. For embedded deployment, output shape constants are not metadata hidden in Python anymore; they become compile-time C configuration that must match the exported graph.

### 5. Channel Layout Bug

This was one of the biggest integration bugs.

Camera output:

```text
HWC / interleaved RGB
RGB RGB RGB ...
```

Model input:

```text
CHW / channel-first
RRR... GGG... BBB...
```

Why generic worked:

- The generic model path did not require the same channel-first conversion.
- That made the camera look innocent at first.

Fix:

- Detect channel-first input from the STAI network flags.
- Capture camera pixels into a separate DMA buffer.
- Transpose HWC to CHW into `nn_in`.
- Clean/invalidate cache around DMA and NPU access.

Design-review framing:

> The model was seeing a scrambled image, not because the camera image looked bad to us, but because the tensor memory layout was wrong. The human-visible UVC frame and the model-visible tensor are different products of the camera pipeline.

### 6. Display Coordinate Bug

The UVC screen is `320x240`, but the camera layer is a centered `240x240` crop:

```text
fg=240x240@40,0
bg=240x240@40,0
```

When the foreground overlay was expanded to the full screen, boxes were drawn in the black area. That made detections look nonsensical even when postprocess was improving.

Fix:

- Keep foreground and background layers aligned.
- Draw boxes relative to the camera layer, not the full UVC screen.
- Clear the overlay before each frame to avoid stale boxes.

Design-review framing:

> The detector was not the only source of visual truth. The display compositor had its own coordinate frame, and mismatching that frame created false visual evidence.

### 7. Noise And Threshold Tuning

Initial low thresholds created many `plate` labels and made the video unreadable.

Current tuning:

```c
#define AI_OD_OBB_PP_CONF_THRESHOLD      (0.30f)
#define AI_OD_OBB_PP_IOU_THRESHOLD       (0.40f)
#define AI_OD_OBB_PP_CANDIDATES_LIMIT    (24)
#define AI_OD_OBB_PP_MAX_BOXES_LIMIT     (2)
```

Design-review framing:

> For bring-up, we intentionally limited output to a small number of boxes. This made it easier to distinguish integration failures from normal model uncertainty.

### 8. Performance Instrumentation

Instead of one vague "inference FPS" number, the firmware now separates:

- NN-only inference time
- preprocess + NN + postprocess compute time
- UVC/display overhead
- camera wait time
- estimated headless loop time

Example:

```text
TRACE: perf: frame=1050 det=2 loop=67ms/14.9fps headless_est=58ms/17.2fps nn=39ms/25.6fps compute=44ms/22.7fps prep=2 post=3 uvc_display=9 cam_wait=14
```

How to explain it:

- `nn`: the NPU model runtime.
- `compute`: useful detection compute, including preprocessing and postprocess.
- `loop`: debug/UVC end-to-end loop.
- `headless_est`: estimated loop without UVC drawing.
- `cam_wait`: how much camera capture was not hidden by overlap.

Design-review framing:

> We avoided a single misleading FPS number. Real-time vision has multiple latencies: model latency, compute latency, camera pipeline latency, and debug display latency.

## Current FPS/Efficiency Story

Based on observed serial output at 320x320:

- NN-only inference: roughly `25-32 FPS`, depending on frame and measurement conditions.
- Preprocess + inference + postprocess: roughly `22 FPS`.
- UVC/debug loop: roughly `15-21 FPS`.
- Estimated headless loop: roughly `17-22 FPS`.

The NPU is doing the heavy part. Preprocessing and postprocess are relatively small, often only a few milliseconds together. UVC display/debug and camera pacing are meaningful overheads, which is why headless operation should be evaluated separately from UVC demo mode.

Strong interview phrasing:

> The model is not yet 30 FPS end-to-end at 320, but the NPU inference itself is near that range. The full debug loop is slower because it includes camera pacing and UVC rendering. In the real robot, we would run headless, avoid UVC drawing, and use temporal holding/prediction between detections.

## Why MCU Can Still Compete With Jetson

A Jetson has far more raw compute, but the MCU has a different advantage:

- no operating system scheduling jitter in the hot loop
- no ROS/image-copy pipeline unless we add one externally
- direct sensor-to-accelerator pipeline
- deterministic memory ownership
- lower power
- easier headless real-time control integration

The STM32N6 does not beat a Jetson on raw model throughput for a large model. It competes by reducing system overhead and putting detection closer to the control loop.

Strong interview phrasing:

> The Jetson wins if the question is maximum neural network throughput. The MCU becomes compelling when the question is bounded-latency embedded perception close to the actuator/control loop, especially when the model is small enough and the pipeline avoids OS and middleware overhead.

## AXISRAM Explanation

AXISRAM is the fast internal SRAM region used by the running firmware. In this project it holds:

- app code/data loaded for execution
- stacks and globals
- camera capture buffers
- NN input/output/runtime state
- display foreground/background buffers
- postprocess state

The 320 build is tight:

```text
AXISRAM1_S: ~1.676 MB / 1647 KB = 99.36%
```

This matters because camera/inference overlap needs an extra camera buffer. That buffer improves latency by letting capture happen while inference runs, but it consumes RAM. The project currently fits, but only with a small margin.

Strong interview phrasing:

> We traded RAM for latency. The extra capture buffer lets us overlap camera acquisition with inference, but it pushes AXISRAM near capacity. That is an embedded design tradeoff: predictable real-time behavior often costs memory.

## Camera/Inference Overlap

Before overlap, the loop behaved like:

```text
capture -> preprocess -> inference -> postprocess -> display -> capture again
```

After overlap:

```text
capture frame N
preprocess frame N
start capture frame N+1
inference frame N while camera captures N+1
postprocess/display frame N
wait only if frame N+1 is not done
```

`cam_wait=0` means overlap fully hid the next camera capture. Nonzero `cam_wait` means the app finished its other work before the next frame was ready.

Strong interview phrasing:

> The overlap does not make the NPU itself faster. It improves pipeline throughput by hiding camera acquisition behind inference and display work.

## What I Would Improve Next

Near-term engineering improvements:

- Print box coordinates in pixels and with more decimal precision.
- Add temporal smoothing/holding so detections do not flicker frame-to-frame.
- Add a headless benchmark mode with UVC disabled.
- Tune thresholds using real match footage and deployment camera captures.
- Quantize/calibrate with more representative images from the actual camera.

Model/architecture improvements:

- Train or export a smaller NPU-friendly detector.
- Avoid graph operations that produce software/hybrid epochs where possible.
- Test 288 or 320 variants with better architecture rather than only shrinking input size.
- Consider an axis-aligned detector if rotation is not needed for control.

Product-level improvements:

- Send compact detections over UART/CAN instead of video overlays.
- Integrate detection holding/prediction with turret/gimbal control.
- Measure photon-to-control latency with GPIO timestamping or synchronized test signals.

## Common Questions And Good Answers

**Why not just use the Jetson?**

Jetson is better for large models and flexible CV stacks, but this project explores a lower-power, lower-overhead perception path directly on an MCU. For small enough models, the STM32N6 can provide predictable latency close to the control loop.

**Was the model bad at first?**

Not necessarily. The biggest failures were integration errors: wrong tensor layout, wrong output constants, display coordinate mismatch, and too much debug noise. Once those were fixed, the same model started producing usable boxes.

**Why did boxes show up in black areas?**

The overlay layer and camera layer had different coordinate systems. Boxes were being drawn relative to the full UVC screen while the camera image occupied only a centered crop.

**What does `headless_est` mean?**

It estimates the loop FPS if the UVC display drawing cost were removed. Real robot operation would likely be headless, so this is more relevant than the visible UVC demo FPS.

**What does `nn=39ms` mean?**

It is only the neural network execution time, not full camera-to-detection latency. End-to-end latency also includes exposure, sensor readout, capture, preprocessing, postprocessing, output transport, and control-loop consumption.

**Why is 320x320 a reasonable current choice?**

It fits memory, detects plates live, and gives near-usable real-time performance. Larger inputs may improve accuracy but can exceed memory or reduce FPS. Smaller inputs can improve speed but may hurt small/distant plate detection.

**What is the best single sentence for the project?**

This project turns a RoboMaster armor-plate detector into a constrained real-time embedded vision pipeline on STM32N6, solving the practical gaps between a trained model and a working camera-to-NPU-to-control deployment.

## Design Review Checklist

Use this checklist before presenting a result:

- Confirm camera boot lines show IMX219 detected.
- Confirm active profile is `SafalObb`.
- Confirm model input is `1x3x320x320`.
- Confirm output length is `12600` bytes and postprocess total boxes is `2100`.
- Confirm `TRACE: NN input` has `pitch=960`.
- Confirm display geometry is `fg=240x240@40,0 bg=240x240@40,0`.
- Confirm `TRACE: detect` prints real confidence and plausible coordinates.
- Report NN-only, compute, UVC loop, and headless-estimated FPS separately.
- Explain that UVC is a debug/demo path, not the final robot-control path.
- Mention AXISRAM margin if discussing future features.
