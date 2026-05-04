# Local Build Helpers

This folder adds a Windows-first wrapper around the repo's existing Makefile flow.

## Quick Start

From the repository root:

```powershell
.\build.ps1
```

That builds the `NUCLEO-N657X0-Q` application in `UVCL` mode.

## Common Commands

Build only:

```powershell
.\build.ps1
```

The helper defaults to `-Jobs 1` for reliability on Windows. If you want a faster build and your machine handles it cleanly, you can raise it manually:

```powershell
.\build.ps1 -Jobs 2
```

Build and sign:

```powershell
.\scripts\stm32n6.ps1 -Action sign
```

First-time flash for the Nucleo board:

```powershell
.\flash.ps1
```

Update only the signed application image in external flash:

```powershell
.\flash.ps1 -AppOnly
```

Build the Nucleo SPI variant:

```powershell
.\build.ps1 -Interface SPI
```

Clean and rebuild:

```powershell
.\build.ps1 -Clean
```

## What It Detects Automatically

- STM32CubeIDE bundled `make`, `sh`, and `arm-none-eabi-gcc`
- STM32CubeProgrammer CLI
- STM32 signing tool
- The correct external loader for the Nucleo board

## Notes

- `flash-all` programs `FSBL/ai_fsbl.hex`, `Model/<board>/network_data.hex`, and the signed application image.
- `flash-app` only updates `Project_sign.bin` at `0x70100000`.
- Put the board in development mode before flashing.
