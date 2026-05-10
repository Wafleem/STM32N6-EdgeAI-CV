# ST AiRunner Board Validation

This repo has two different board-test paths:

- The normal camera/UVC firmware streams video, runs the model from camera frames, and emits human-readable traces.
- ST AiRunner / `stedgeai validate` talks to an `aiValidation` firmware over a binary protobuf serial protocol.

Those are not the same protocol. AiRunner cannot talk to the normal camera app unless we add a dedicated validation mode.

## What We Added

Use `scripts/stm32n6-ai-validation.ps1` to prepare ST's NPU validation firmware
for this repo's generated BestMerge model artifacts:

```powershell
.\scripts\stm32n6-ai-validation.ps1 -Action build -Clean
```

That creates a temporary validation workspace at:

```text
C:\tmp\stm32n6-ai-validation
```

and produces the RAM-linked validation firmware at:

```text
C:\tmp\stm32n6-ai-validation\Projects\STM32N6570-DK\Applications\NPU_Validation\armgcc\build\N6-Nucleo\Project.hex
```

To deploy the validation setup over SWD:

```powershell
.\scripts\stm32n6-ai-validation.ps1 -Action flash
```

That flashes this repo's `network_data.hex` to external NOR, then loads and
starts the aiValidation firmware in SRAM.

After the validation firmware is running, use `scripts/test-ai-runner-board.ps1`
to exercise ST's host-side AiRunner tools:

```powershell
.\scripts\test-ai-runner-board.ps1 -Desc serial:COM16:921600
```

For a connection/model summary only:

```powershell
.\scripts\test-ai-runner-board.ps1 -Action summary -Desc serial:COM16:921600
```

To send a small batch of preprocessed images from the PC to the board:

```powershell
.\scripts\test-ai-runner-board.ps1 `
  -Action validate `
  -Desc serial:COM16:921600 `
  -ValInput Model\validation_inputs\bestmerge_v4_uint8_320_n10.npy `
  -BatchSize 10
```

The script defaults to:

- Model: `Model\bestmerge_320_robomaster_v4_clean_qdq.onnx`
- Target: `stm32n6`
- Descriptor: `serial:921600`
- Mode: `target-io-only`
- Input data type: `uint8`
- Output data type: `int8`

Validation inputs are flattened `uint8` tensors, not raw `.jpg` files. The
current prepared batch is:

```text
Model\validation_inputs\bestmerge_v4_uint8_320_n10.npy
shape=(10, 307200), dtype=uint8
```

It was generated from the clean RoboMaster v4 image subset with:

```powershell
python Model\prepare-validation-inputs.py `
  Model\calibration_datasets\robomaster_v4_clean_images `
  Model\validation_inputs\bestmerge_v4_uint8_320_n10.npy `
  --manifest Model\validation_inputs\bestmerge_v4_uint8_320_n10.csv `
  --imgsz 320 `
  --count 10
```

## Why Not Run AiRunner Inside Camera Mode?

ST's aiValidation stack expects to own a serial transport. The ST porting header notes that once the AI COM stack is initialized, regular text output on that same serial port is skipped or must be routed to a dedicated print port. Our current firmware uses USART1 for readable boot/camera/inference traces.

If we mix readable trace prints with the AiRunner binary protocol on the same UART, the host will see corrupted protocol frames. If we let aiValidation own the UART all the time, we lose normal live debugging while the camera app is running.

## Recommended Integration Shape

Use a separate validation firmware/profile rather than trying to run both protocols at once:

1. Build the ST NPU_Validation app through `scripts/stm32n6-ai-validation.ps1`.
2. Inject this repo's generated `network.c`, `stai_network.c`, and `network_ecblobs.h`.
3. Use ST's `ai_wrapper_ATON_ST_AI.c` and `aiValidation_ATON_ST_AI.c` path.
4. Include `ll_aton_stai_internal.c`, which is required by generated STAI networks.
5. Flash `network_data.hex` to external NOR.
6. Load and start the validation app in SRAM.
7. Keep human-readable logging off the AiRunner UART while validation is active.

That gives us an honest board-side model test path without destabilizing the working camera app.

## Why Not Port This To CMake?

For this path, CMake would add more surface area than value. ST ships the
NPU_Validation project as a Makefile app, and the tricky parts are not build
system mechanics; they are selecting the correct STAI wrapper sources and
pointing paths at the installed ST runtime libraries. The repo script patches
those specifics while leaving the ST project structure intact.

## Porting Pieces Needed

The ST Edge AI 4.0 install provides the protocol pieces under:

```text
C:\ST\STEdgeAI\4.0\Middlewares\ST\AI\Validation
C:\ST\STEdgeAI\4.0\Middlewares\ST\AI\Misc
```

The likely N6/STAI runtime entry path is:

```text
Validation\Src\aiValidation_ATON_ST_AI.c
Validation\Src\ai_wrapper_ATON_ST_AI.c
Validation\Src\ai_io_buffers_ATON.c
Validation\Src\aiPb*.c
Validation\Src\pb_*.c
Validation\Src\stm32msg.pb.c
Misc\Src\aiTestUtility.c
Misc\Src\aiTestHelper*.c
Misc\Src\ai_device_adaptor.c
```

The repo already contains ST's `SystemPerformance` sources, which are useful for profiling but are not the full AiRunner host protocol by themselves.

## Logger Interaction

The local logger module lives in `modules/logger`. It should stay enabled for normal camera firmware and mostly disabled for `APP_ENABLE_AI_VALIDATION=1`, unless logs are routed to a separate UART. This prevents logger output from corrupting AiRunner protobuf packets.
