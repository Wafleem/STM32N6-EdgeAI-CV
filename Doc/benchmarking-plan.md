# Benchmarking Plan

This document defines how to compare the STM32N6 armor-plate detector against a Jetson deployment without mixing model-only latency, camera latency, middleware overhead, and debug-display overhead.

## Current STM32N6 Baseline

Current preliminary STM32N6 numbers come from live serial traces in the `SafalObb` 320 UVC/debug build:

| Metric | Preliminary value | Source |
| :----- | ----------------: | :----- |
| NN-only inference | `25-32 FPS` | `TRACE: perf ... nn=...` |
| Compute path | `~22 FPS` | `prep + nn + post`, reported as `compute=...` |
| Headless-estimated loop | `17-22 FPS` | `headless_est=...`, subtracts UVC drawing cost |
| UVC/debug loop | `15-21 FPS` | `loop=...`, includes display/stream overhead |
| AXISRAM usage | `99.36%` | Firmware build map, about `1.676 MB` used |

The headline number for resumes can be NN-only inference, but design reviews should also show compute-path and headless-loop numbers.

## Metrics To Report

Report these categories separately:

- **Model-only latency:** neural network execution only.
- **Compute-path latency:** preprocessing + neural network + postprocess.
- **Camera-to-detection latency:** camera exposure/readout + capture + compute + detection output.
- **Debug/display latency:** UVC drawing/streaming or visualization overhead.
- **Control-path latency:** detection output to downstream aiming/control logic.

Avoid comparing STM32 `nn=...` directly against Jetson camera-to-ROS latency. That would make the MCU look artificially good and would not be technically fair.

## STM32N6 Measurements

Use the serial parser:

```powershell
python .\scripts\benchmark\parse-serial-perf.py .\serial.log --markdown
```

Collect at least:

- 30 seconds with no target.
- 30 seconds with a static armor plate.
- 30 seconds with hand/camera motion.
- 30 seconds with cluttered background.

For the current UVC build, report:

- `nn_ms` and `nn_fps`
- `compute_ms` and `compute_fps`
- `loop_ms` and `loop_fps`
- `headless_est_ms` and `headless_est_fps`
- `cam_wait_ms`
- detection count and confidence distribution where available

For a real headless build, disable UVC drawing and report measured loop time directly instead of relying on `headless_est`.

## Jetson Measurements

Use the same exported model and input size when measuring apples-to-apples model performance:

- same checkpoint/model lineage
- same input size, ideally `320x320`
- same class set
- equivalent quantization/runtime path where possible
- same or equivalent postprocess thresholds

Recommended Jetson categories:

- TensorRT model-only inference time.
- Preprocess + TensorRT + postprocess time.
- Full camera pipeline time.
- Full ROS/image-transport path time, if ROS is part of deployment.

If the Jetson comparison uses its original `640x640` TensorRT engine, label it as a deployment comparison, not an apples-to-apples model comparison.

## Results Table Template

| Platform | Model/Input | Model-only FPS | Compute FPS | Camera-to-detection latency | Notes |
| :------- | :---------- | -------------: | ----------: | --------------------------: | :---- |
| STM32N6 UVC debug | BestMerge OBB `320x320` | `25-32` preliminary | `~22` preliminary | TBD | UVC drawing included in full loop |
| STM32N6 headless | BestMerge OBB `320x320` | TBD | TBD | TBD | Planned no-UVC build |
| Jetson model-only | Same `320x320` export | TBD | TBD | N/A | Planned TensorRT/ORT measurement |
| Jetson deployment | Existing robot engine | TBD | TBD | TBD | May use different input size/runtime |

## Resume Guidance

Safe current wording:

```latex
\resumeItem{Ported a YOLO-based armor-plate detector from a Jetson-class CV workflow to an STM32N6 MCU/NPU pipeline, achieving 25--32 FPS NN-only inference at 320x320 with IMX219 camera input and custom C OBB postprocessing.}
```

Do not use this until measured:

```latex
\resumeItem{Benchmarked latency and accuracy against the original NVIDIA Jetson Orin Nano deployment.}
```

Replace it later with a measured statement that names the benchmark category.
