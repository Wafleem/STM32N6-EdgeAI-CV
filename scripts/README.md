# Scripts

This folder contains Windows-first helpers for building, flashing, converting models, generating STM32N6 artifacts, and summarizing serial benchmark logs.

## Firmware Build And Flash

Build the active armor-plate profile:

```powershell
.\scripts\stm32n6.ps1 -Action build -ModelProfile SafalObb -Jobs 2
```

Flash FSBL, model weights, and signed app:

```powershell
.\scripts\stm32n6.ps1 -Action flash-all -ModelProfile SafalObb -Jobs 2
```

Update only the signed app image:

```powershell
.\scripts\stm32n6.ps1 -Action flash-app -ModelProfile SafalObb -Jobs 2
```

The wrapper detects STM32CubeIDE tools, STM32CubeProgrammer, the signing tool, and the Nucleo external loader.

## Model Conversion Happy Path

Export an Ultralytics OBB checkpoint to ONNX:

```powershell
.\scripts\model\export-obb-onnx.ps1 `
  -Checkpoint Model\bestmerge.pt `
  -OutputOnnx Model\bestmerge_320.onnx `
  -ImageSize 320 `
  -ExpectedClasses 1 `
  -ExpectedBoxes 2100
```

For the verified experimental `288x288` artifact, use `-ImageSize 288` and
`-ExpectedBoxes 1701`. The active deployed firmware profile remains the
`320x320` model unless `app_config.h` and generated ST artifacts are changed
together.

Prepare calibration tensors and quantize to QDQ ONNX:

```powershell
.\scripts\model\quantize-obb-onnx.ps1 `
  -InputOnnx Model\bestmerge_320.onnx `
  -CalibrationImages Model\calibration_images\robomaster_v4_clean `
  -ImageSize 320 `
  -QdqOutput Model\bestmerge_320_robomaster_v4_clean_qdq.onnx `
  -BoundaryOutput Model\bestmerge_320_robomaster_v4_clean_uint8in_int8out_qdq.onnx
```

Generate and package STM32N6 artifacts:

```powershell
.\scripts\model\generate-stm32n6-artifacts.ps1 `
  -ModelOnnx Model\bestmerge_320_robomaster_v4_clean_qdq.onnx `
  -OutputProfileDir Model\NUCLEO-N657X0-Q_SafalObb
```

Then rebuild and flash:

```powershell
.\scripts\stm32n6.ps1 -Action flash-all -ModelProfile SafalObb -Jobs 2
```

## Benchmark Parsing

Capture serial output to a file, then summarize `TRACE: perf` lines:

```powershell
python .\scripts\benchmark\parse-serial-perf.py .\serial.log --markdown
```

The parser reports min, mean, p95, and max for:

- full UVC/debug loop
- headless-estimated loop
- NN-only inference
- preprocess + inference + postprocess compute path
- preprocessing
- postprocess
- UVC display
- camera wait

## Validation Helpers

AiRunner board validation helpers are kept for ST `aiValidation`-style firmware:

```powershell
.\scripts\test-ai-runner-board.ps1 -Action summary -Desc serial:COM16:921600
```

AiRunner does not talk to the normal camera/UVC application firmware. See [../Doc/AiRunner-Board-Validation.md](../Doc/AiRunner-Board-Validation.md).

## Tooling Notes

- ST Edge AI is expected at `C:\ST\STEdgeAI\4.0\Utilities\windows\stedgeai.exe`, or available on `PATH`.
- STM32CubeIDE provides `make`, `sh`, `arm-none-eabi-gcc`, and `arm-none-eabi-objcopy`.
- STM32CubeProgrammer provides the flashing and signing tools.
- Python model scripts can use local dependency folders under `Model\evaldeps`, `Model\pydeps_onnx`, or `Model\onnxdeps_local` when present.
