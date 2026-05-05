# Codex Handoff: STM32N6 EdgeAI CV

Last updated: 2026-05-03

## Repo Status

This repository is the customized project repo:

```text
https://github.com/Wafleem/STM32N6-EdgeAI-CV.git
```

Use this repo as the source of truth. The ST upstream sample is only the original base project.

Latest known pushed commits at handoff:

```text
f63565f Customize README for IMX219 bring-up
146371f Add IMX219 camera bring-up tracing
8c5f585 Fix Windows build workflow and document deployment
5d08942 Add gitattributes for consistent line endings
2877ab1 Add IMX219 support and initialize project repo
```

The clean publish worktree used for the final push was:

```text
C:\tmp\stm32n6-edgeai-publish
```

The earlier working clone at:

```text
C:\Users\saysa\Documents\Robomaster_CodeStuff\stm32n6-sample\STM32N6-GettingStarted-ObjectDetection
```

had unrelated unstaged local changes and generated/deleted files during bring-up. Do not assume that older clone is clean. Start new Codex work from the GitHub repo or from a fresh clone of `Wafleem/STM32N6-EdgeAI-CV`.

## Hardware That Works

Known working setup:

- Board: `NUCLEO-N657X0-Q`
- Camera: Sony `IMX219`
- Tested camera module: Arducam Raspberry Pi IMX219 module
- Output: USB/UVC from `CN8`
- Debug/programming: onboard ST-LINK through `CN9`
- Serial trace: `COM16`, `115200` baud during the successful bring-up session
- STM32CubeProgrammer version used successfully: `2.19`

The key discovery was that the Arducam/Raspberry Pi IMX219 module does work electrically with this board setup. The board successfully read the IMX219 chip ID:

```text
TRACE: IMX219_ReadID: combined id=0x0219
```

## What Was Fixed

The original failure was not that the board could not see the camera. I2C worked and the chip ID was correct.

The boot failure happened because the camera middleware passed `CMW_PIXEL_FORMAT_RAW10` (`0x2B`) directly into the low-level IMX219 component driver. The low-level driver expected its own enum value, `IMX219_RAW_RGGB10`.

The fix maps middleware pixel formats to component pixel formats before calling `IMX219_Init`.

Expected mapping:

```text
CMW_PIXEL_FORMAT_DEFAULT -> IMX219_RAW_RGGB10
CMW_PIXEL_FORMAT_RAW10   -> IMX219_RAW_RGGB10
CMW_PIXEL_FORMAT_RAW8    -> IMX219_RAW_RGGB8
```

The repo also has boot tracing added so camera failures are visible instead of ending at a generic assert.

## Important Files

Camera and pipeline bring-up files:

- `Application/NUCLEO-N657X0-Q/Src/main.c`
- `Application/NUCLEO-N657X0-Q/Src/app_camerapipeline.c`
- `Middlewares/stm32-mw-camera/cmw_camera.c`
- `Middlewares/stm32-mw-camera/sensors/cmw_imx219.c`
- `Middlewares/stm32-mw-camera/sensors/imx219/imx219.c`

Docs and workflow files:

- `README.md`
- `build.ps1`
- `flash.ps1`
- `scripts/stm32n6.ps1`
- `scripts/README.md`

The README was customized to describe this as the `STM32N6 EdgeAI CV` project, not just the base ST getting-started sample.

## Known-Good Boot Trace

These lines mean the camera path is healthy:

```text
TRACE: IMX219_ReadID: combined id=0x0219
TRACE: CMW_CAMERA_Probe_Sensor: selected IMX219
TRACE: CMW_CAMERA_Start: Camera_Drv.Start ret=0
TRACE: CameraPipeline_DisplayPipe_Start: ret=0
TRACE: CameraPipeline_NNPipe_Start: ret=0
```

A successful session also showed repeated NN pipe starts:

```text
TRACE: CameraPipeline_NNPipe_Start: ret=0 dst=0x342e0000 mode=4 count=0
TRACE: CameraPipeline_NNPipe_Start: ret=0 dst=0x342e0000 mode=4 count=1
```

## Flashing Notes

For first-time programming, the board must be in development mode.

The important flash pieces are:

```text
FSBL/ai_fsbl.hex
Model/NUCLEO-N657X0-Q/network_data.hex
Application/NUCLEO-N657X0-Q/build/Project_sign.bin
```

Address reminders:

```text
FSBL:  0x70000000
App:   0x70100000
Model: 0x70380000
```

After flashing, put the board in boot-from-flash mode and power-cycle it.

For USB/UVC video:

- `CN9` is ST-LINK/debug/programming.
- `CN8` is the USB/UVC camera stream port.
- If no USB camera enumerates, read the serial trace first. A camera init failure can stop the app before USB enumerates.

## Useful Commands

Build from repo root on Windows:

```powershell
.\build.ps1 -Jobs 1
```

Sign:

```powershell
.\scripts\stm32n6.ps1 -Action sign -Jobs 1
```

Full flash:

```powershell
.\flash.ps1
```

Application-only flash after FSBL/model are already programmed:

```powershell
.\flash.ps1 -AppOnly
```

If using STM32CubeProgrammer directly, the validated install path was:

```text
C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer--V219\bin
```

## Next Codex Session Guidance

Start by confirming:

```powershell
git remote -v
git status --short
git log --oneline -5
```

Expected remote:

```text
origin https://github.com/Wafleem/STM32N6-EdgeAI-CV.git
```

Recommended first task in a new session:

1. Fresh clone `Wafleem/STM32N6-EdgeAI-CV`.
2. Build with `.\build.ps1 -Jobs 1`.
3. If hardware is connected, sign and app-flash.
4. Confirm serial trace reaches `selected IMX219` and `Camera_Drv.Start ret=0`.

Avoid force-pushing unless the user explicitly asks. During this handoff session the user explicitly requested forcing the local work into `main`, and the final push used `--force-with-lease`.

