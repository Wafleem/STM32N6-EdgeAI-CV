# ST AiRunner Board Validation

This repo has two different board-test paths:

- The normal camera/UVC firmware streams video, runs the model from camera frames, and emits human-readable traces.
- ST AiRunner / `stedgeai validate` talks to an `aiValidation` firmware over a binary protobuf serial protocol.

Those are not the same protocol. AiRunner cannot talk to the normal camera app unless we add a dedicated validation mode.

## What We Added

Use `scripts/test-ai-runner-board.ps1` to exercise ST's host-side AiRunner tools:

```powershell
.\scripts\test-ai-runner-board.ps1 -Desc serial:COM16:921600
```

For a connection/model summary only:

```powershell
.\scripts\test-ai-runner-board.ps1 -Action summary -Desc serial:COM16:921600
```

The script defaults to:

- Model: `Model\bestmerge_320_robomaster_v3_qdq.onnx`
- Target: `stm32n6`
- Descriptor: `serial:921600`
- Mode: `target-io-only`
- Input data type: `uint8`
- Output data type: `int8`

## Why Not Run AiRunner Inside Camera Mode?

ST's aiValidation stack expects to own a serial transport. The ST porting header notes that once the AI COM stack is initialized, regular text output on that same serial port is skipped or must be routed to a dedicated print port. Our current firmware uses USART1 for readable boot/camera/inference traces.

If we mix readable trace prints with the AiRunner binary protocol on the same UART, the host will see corrupted protocol frames. If we let aiValidation own the UART all the time, we lose normal live debugging while the camera app is running.

## Recommended Integration Shape

Use a compile-time validation firmware/profile rather than trying to run both protocols at once:

1. Build `APP_ENABLE_AI_VALIDATION=1`.
2. Initialize clock, UART, CRC, Neural-ART memory, cache, security, and external NOR.
3. Skip camera, display, UVC, postprocess, and text trace loops.
4. Call ST's `aiValidationInit()` once.
5. Loop on `aiValidationProcess()`.
6. Keep human-readable logging off the AiRunner UART while validation is active.

That gives us an honest board-side model test path without destabilizing the working camera app.

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
