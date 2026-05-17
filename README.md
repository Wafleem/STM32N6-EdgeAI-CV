# STM32N6 Armor Plate Detection

Real-time RoboMaster armor-plate detection on an STM32N6 MCU/NPU, built as a low-latency embedded perception prototype for closed-loop aiming. This project ports a YOLO-style oriented-bounding-box detector from a Jetson-class CV workflow into a deterministic STM32N6 pipeline with IMX219 camera input, ST Edge AI model generation, custom C postprocessing, camera/inference overlap, and USB/UVC debug visualization.

The current live profile runs a `320x320` quantized armor-plate detector on the `NUCLEO-N657X0-Q` and draws stable plate boxes in the UVC debug stream.

## Why This Project

ST's STM32N6 release made it realistic to evaluate MCU-class neural acceleration for robotics perception workloads that are usually handled by SBCs or GPUs. RoboMaster armor-plate detection is a useful test case because closed-loop aiming cares about bounded latency, camera/control integration, and system overhead, not only peak neural-network throughput.

I used the Nucleo board as a fast prototype target before custom hardware. That let me bring up the camera, convert and quantize the model, validate the NPU runtime, debug the full camera-to-model path, and measure where latency was actually being spent.

## System Architecture

```text
IMX219 camera
  -> CSI/DCMIPP capture
  -> RGB HWC camera frame
  -> HWC-to-CHW preprocessing
  -> STM32N6 NPU, uint8(1x3x320x320)
  -> int8(1x6x2100) raw OBB output
  -> custom C decode + thresholding + NMS
  -> UVC debug overlay now, compact control output later
```

Current active profile:

| Item | Value |
| :--- | :---- |
| Board | `NUCLEO-N657X0-Q` |
| Camera | Sony `IMX219` Raspberry Pi style module |
| Model profile | `SafalObb` |
| Active model | `BestMerge OBB 320` |
| Input tensor | `uint8(1x3x320x320)` |
| Output tensor | `int8(1x6x2100)` |
| Class table | `plate` |
| Display/debug | USB/UVC, `240x240` camera crop centered in `320x240` stream |

## Key Engineering Contributions

- Brought up the IMX219 camera path on the STM32N6 Nucleo flow, including sensor probe diagnostics, CSI/DCMIPP validation, and UVC visibility.
- Converted the armor-plate model into an STM32N6-runnable path with ONNX export, calibration tensor prep, QDQ quantization, and ST Edge AI/Neural-ART artifact generation.
- Implemented custom MCU-side OBB postprocessing for the raw YOLO-style output tensor, including thresholding, candidate filtering, NMS, and display-safe box conversion.
- Fixed the critical camera/model layout mismatch: the camera provides interleaved RGB HWC bytes, while the model expects CHW input.
- Fixed display-coordinate bugs that caused boxes to appear in black UVC regions by aligning foreground overlay geometry with the camera crop.
- Added camera/inference overlap so the next camera frame can be captured while the NPU processes the current frame.
- Added performance tracing that separates NN-only inference, compute path, UVC/debug loop, display overhead, camera wait, and headless-estimated FPS.

## Preliminary Performance

These are live STM32N6 observations from the current `320x320` UVC debug build. Jetson apples-to-apples benchmarking is planned but not yet claimed.

| Metric | Preliminary STM32N6 Result | Notes |
| :----- | -------------------------: | :---- |
| NN-only inference | `25-32 FPS` | Strongest measured model-only number from serial `nn=...` traces |
| Compute path | `~22 FPS` | Preprocess + NPU inference + postprocess |
| Headless-estimated loop | `17-22 FPS` | Estimate subtracting UVC drawing cost; true headless benchmark pending |
| UVC/debug loop | `15-21 FPS` | Includes drawing, streaming, and camera wait |
| AXISRAM usage | `99.36%` | Tight but valid; overlap trades RAM for lower wait time |

Representative trace:

```text
TRACE: perf: frame=1050 det=2 loop=67ms/14.9fps headless_est=58ms/17.2fps nn=39ms/25.6fps compute=44ms/22.7fps prep=2 post=3 uvc_display=9 cam_wait=14
```

The headline `25-32 FPS` number is NN-only inference, not photon-to-control latency. For control-system discussions, use the compute-path and headless-loop numbers.

## Design Decisions

- **Why STM32N6:** evaluate whether MCU-class NPU acceleration can deliver bounded perception latency closer to the control loop than a heavier Linux/GPU stack.
- **Why Nucleo first:** de-risk camera, model, NPU runtime, display, and flashing before considering custom robot hardware.
- **Why 320x320:** larger exports stressed or exceeded memory allocation, while `320x320` fit and produced live detections with usable speed.
- **Why custom postprocess:** the exported OBB model emits raw prediction tensors, not final boxes, so decode and NMS must run in C.
- **Why camera buffering:** the extra capture buffer allows camera acquisition to overlap NPU inference, reducing exposed camera wait at the cost of AXISRAM.
- **Why headless benchmarking matters:** UVC is a debug/demo path; a robot deployment should send compact detections to control code without drawing video overlays.

## Build, Flash, And Model Conversion

Build the active armor detector profile:

```powershell
.\scripts\stm32n6.ps1 -Action build -ModelProfile SafalObb -Jobs 2
```

Flash FSBL, model weights, and signed application:

```powershell
.\scripts\stm32n6.ps1 -Action flash-all -ModelProfile SafalObb -Jobs 2
```

Happy path from model to firmware artifacts:

```powershell
.\scripts\model\export-obb-onnx.ps1 `
  -Checkpoint Model\bestmerge.pt `
  -OutputOnnx Model\bestmerge_320.onnx `
  -ImageSize 320 `
  -ExpectedClasses 1 `
  -ExpectedBoxes 2100

.\scripts\model\quantize-obb-onnx.ps1 `
  -InputOnnx Model\bestmerge_320.onnx `
  -CalibrationImages Model\calibration_images\robomaster_v4_clean `
  -ImageSize 320 `
  -QdqOutput Model\bestmerge_320_robomaster_v4_clean_qdq.onnx `
  -BoundaryOutput Model\bestmerge_320_robomaster_v4_clean_uint8in_int8out_qdq.onnx

.\scripts\model\generate-stm32n6-artifacts.ps1 `
  -ModelOnnx Model\bestmerge_320_robomaster_v4_clean_qdq.onnx `
  -OutputProfileDir Model\NUCLEO-N657X0-Q_SafalObb
```

Summarize serial performance logs:

```powershell
python .\scripts\benchmark\parse-serial-perf.py .\serial.log --markdown
```

See [scripts/README.md](scripts/README.md) for tool requirements and detailed usage.

## Repository Map

| Path | Purpose |
| :--- | :------ |
| [Application/NUCLEO-N657X0-Q](Application/NUCLEO-N657X0-Q) | STM32N6 application, camera/NPU/UVC integration |
| [Model/bestmerge_288*.onnx](Model) | Verified experimental `288x288` raw-OBB artifacts, output `[1, 6, 1701]` |
| [Model/NUCLEO-N657X0-Q_SafalObb](Model/NUCLEO-N657X0-Q_SafalObb) | Generated ST Edge AI artifacts for the active armor model |
| [scripts](scripts) | Build, flash, model conversion, and benchmark helpers |
| [Doc/design-review-notes.md](Doc/design-review-notes.md) | Interview/design-review explanation of the project |
| [Doc/embedded-porting-notes.md](Doc/embedded-porting-notes.md) | Current technical porting notes and root-cause fixes |
| [Doc/bringup-log.md](Doc/bringup-log.md) | Chronological bring-up and debugging log |
| [Doc/benchmarking-plan.md](Doc/benchmarking-plan.md) | STM32 vs Jetson benchmark methodology |

## Benchmarking Plan

Final Jetson comparison is intentionally not claimed yet. The planned benchmark separates model-level and system-level latency so the comparison is fair:

- STM32 model-only: serial `nn=...`.
- STM32 compute path: serial `compute=...`.
- STM32 UVC/debug loop: serial `loop=...`.
- STM32 headless: future no-UVC build, measured directly rather than estimated.
- Jetson model-only: same exported model/input size through TensorRT or ONNX Runtime/TensorRT.
- Jetson system path: camera capture + preprocessing + inference + postprocess + ROS/image transport if used.

The README will be updated with Jetson numbers only after the same model/input-size methodology is measured.

## Resume Snippet

Current resume-safe phrasing:

```latex
\resumeItem{Ported a YOLO-based armor-plate detector from a Jetson-class CV workflow to an STM32N6 MCU/NPU pipeline, achieving 25--32 FPS NN-only inference at 320x320 with IMX219 camera input and custom C OBB postprocessing.}
```

Future phrasing after real Jetson benchmarking:

```latex
\resumeItem{Benchmarked STM32N6 headless latency against Jetson Orin Nano TensorRT deployment across model-only, compute-path, and camera-to-detection latency.}
```

## Status

The current `SafalObb` 320 profile builds, flashes, runs live over UVC, and detects armor plate images with visible boxes. Remaining polish work is focused on headless benchmarking, model-quality tuning, and cleaner production output paths.
