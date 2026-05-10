# STM32N6 EdgeAI CV

---

This repository is a customized STM32N6 object-detection bring-up project based on ST's `STM32N6-GettingStarted-ObjectDetection` example. The goal is to keep the original sample easy to recognize while making this fork practical for our hardware path: `NUCLEO-N657X0-Q`, USB/UVC output, and Raspberry Pi style Sony IMX219 camera modules.

The application runs a [STEdgeAI](https://www.st.com/en/development-tools/stedgeai-core.html) generated object-detection model on the STM32N6 NPU, captures frames through DCMIPP/CSI, and streams the processed output over USB/UVC or to the SPI display configuration.

Current working target:

- Board: `NUCLEO-N657X0-Q`
- Camera: Sony `IMX219`, including the Arducam Raspberry Pi IMX219 module tested during bring-up
- Output: USB/UVC from `CN8`
- Debug/programming: ST-LINK on `CN9`
- Programmer used during validation: STM32CubeProgrammer `v2.19`

The fork now includes additional camera boot tracing and IMX219-specific integration fixes that helped validate the camera from first I2C chip-ID read through DCMIPP CSI pipe start.

![Image sample](_htmresc/sample.PNG)
Detected classes and confidence level are displayed on the bounding boxes.

This is a standalone project that can be deployed directly to hardware. It is also integrated into the [ST ModelZoo repository](https://github.com/STMicroelectronics/stm32ai-modelzoo-services), and is required to deploy the object detection use case. The ModelZoo enables you to train, evaluate, and automatically deploy any supported model. If you wish to use this project as part of the ModelZoo, please refer to the [Quickstart using stm32ai-modelzoo-services](#quickstart-using-stm32ai-modelzoo-services) section for instructions.

This README provides an overview of the application. Additional documentation is available in the [Doc](./Doc/) folder.

---

## Table of Contents

- [Features Demonstrated](#features-demonstrated)
- [Project Customizations](#project-customizations)
- [Models](#models)
- [Hardware Support](#hardware-support)
- [Camera Support](#camera-support)
- [Tools Version](#tools-version)
- [Boot Modes](#boot-modes)
- [Quickstart using stm32ai-modelzoo-services](#quickstart-using-stm32ai-modelzoo-services)
- [Quickstart using Prebuilt Binaries](#quickstart-using-prebuilt-binaries)
  - [Programming with STM32CubeProgrammer UI](#how-to-program-hex-files-using-stm32cubeprogrammer-ui)
  - [Programming NUCLEO-N657X0-Q via Command Line](#how-to-program-hex-files-on-nucleo-n657x0-q-using-command-line)
- [Quickstart using Source Code](#quickstart-using-source-code)
  - [Build and Run - Dev Mode](#application-build-and-run---dev-mode)
    - [STM32CubeIDE](#stm32cubeide)
    - [Makefile](#makefile)
  - [Build and Run - Boot from Flash](#application-build-and-run---boot-from-flash)
    - [Build the Application](#build-the-application)
      - [STM32CubeIDE](#stm32cubeide-1)
      - [Makefile](#makefile-1)
    - [Programming Firmware to External Flash](#program-the-firmware-in-the-external-flash)
- [How to update my project with a new version of ST Edge AI](#how-to-update-my-project-with-a-new-version-of-st-edge-ai)
- [Known Issues and Limitations](#known-issues-and-limitations)

**Documentation Folder:**

- [Boot Overview](Doc/Boot-Overview.md)
- [Camera Build Options](Doc/Build-Options.md#cameras-module)
- [Camera Orientation](Doc/Build-Options.md#camera-orientation)
- [Aspect Ratio Mode](Doc/Build-Options.md#aspect-ratio-mode)
- [Neural-ART: Description and Operation](Doc/Neural-ART-Description-and-Operation.md)
- [Deploying your Quantized Model](Doc/Deploy-your-Quantized-Model.md)
- [Safal OBB Porting Notes](Doc/Safal-OBB-Porting.md)
- [Safal OBB Porting Log](Doc/Safal-OBB-Porting-Log.md)
- [Programming Hex Files with STM32CubeProgrammer](Doc/Program-Hex-Files-STM32CubeProgrammer.md)

---

## Features Demonstrated

- Sequential application flow
- NPU-accelerated quantized AI model inference
- Dual DCMIPP pipelines
- DCMIPP cropping, decimation, and downscaling
- DCMIPP ISP usage
- LTDC dual-layer implementation
- Development mode
- Boot from external flash

---

## Project Customizations

This fork keeps the ST sample structure, but adds the pieces needed for the current Nucleo plus IMX219 workflow:

- Windows helper scripts for repeatable build, sign, and flash flows.
- Nucleo USB/UVC bring-up notes for `CN8` video streaming and `CN9` ST-LINK programming.
- More explicit boot diagnostics over the serial console at `115200` baud.
- Sensor probe tracing for each supported camera path so startup failures are visible instead of ending at a generic assert.
- IMX219 bring-up tracing for power, reset, I2C bus registration, chip-ID reads, register-table setup, CSI config, and camera start.
- IMX219 camera integration work for Raspberry Pi style modules, including the CMW-to-component pixel-format mapping required by the low-level IMX219 driver.

The most useful successful boot signs are:

```text
TRACE: IMX219_ReadID: combined id=0x0219
TRACE: CMW_CAMERA_Probe_Sensor: selected IMX219
TRACE: CMW_CAMERA_Start: Camera_Drv.Start ret=0
TRACE: CameraPipeline_DisplayPipe_Start: ret=0
```

If the board reaches those lines, the sensor is responding on I2C and the camera pipeline has moved beyond the early boot failures that originally blocked USB/UVC enumeration.

---

## Models

| Model | Board | Inference time |
| :---- | :---- | -------------: |
| quantized_tiny_yolo_v2_224_.tflite | NUCLEO-N657X0-Q SPI | 30 ms |
| quantized_tiny_yolo_v2_224_.tflite | NUCLEO-N657X0-Q UVCL | 27 ms |

Available local build profiles:

- `Generic`: the upstream tiny YOLOv2 example, using [Model/NUCLEO-N657X0-Q](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Model/NUCLEO-N657X0-Q)
- `SafalObb`: the Nitish/Safal RoboMaster OBB port, using [Model/NUCLEO-N657X0-Q_SafalObb](/C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Model/NUCLEO-N657X0-Q_SafalObb)

The `SafalObb` profile uses the same Nitish checkpoint lineage as Safal's deployed Jetson OBB engine, exported at `224x224` to stay aligned with the smaller ST-style Nucleo object-detection footprint.

---

## Hardware Support

Supported development platform:

- [NUCLEO-N657X0-Q](https://www.st.com/en/evaluation-tools/nucleo-n657x0-q.html) Nucleo Board
  - Connect to the onboard ST-LINK debug adapter (CN9) using a __USB-C to USB-C cable__ for sufficient power.
  - OTP fuses are configured for xSPI IOs to achieve maximum speed (200MHz) on xSPI interfaces.

Supported camera modules:

- Provided IMX335 camera module
- [STEVAL-55G1MBI](https://www.st.com/en/evaluation-tools/steval-55g1mbi.html)
- [STEVAL-66GYMAI](https://www.st.com/en/evaluation-tools/steval-66gymai.html)
- [STEVAL-1943-MC1](https://www.st.com/en/evaluation-tools/steval-1943-mc1.html)
- Sony IMX219 modules supported by this fork, including the Arducam Raspberry Pi IMX219 module used during validation.

For the Nucleo board, one of the following displays is required:

- A USB host for data transmission via USB/UVC (using the USB OTG port CN8)

![Board](_htmresc/NUCLEO-N657X0-Q_USB_UVC.png)
NUCLEO-N657X0-Q board with USB/UVC display.

- [X-NUCLEO-GFX01M2](https://www.st.com/en/evaluation-tools/x-nucleo-gfx01m2.html) SPI display

![Board](_htmresc/NUCLEO-N657X0-Q_SPI.png)
NUCLEO-N657X0-Q board with SPI display.

---

## Camera Support

The original ST sample supports several camera modules. This fork adds practical bring-up support for the IMX219 path used by common Raspberry Pi camera modules.

| Sensor | Status in this fork | Notes |
| :----- | :------------------ | :---- |
| IMX219 | Working on `NUCLEO-N657X0-Q` | Validated with an Arducam Raspberry Pi IMX219 module. Expected chip ID is `0x0219`. |
| IMX335 | Preserved from ST sample | Original ST/DK path remains in the tree. |
| VD55G1 / VD66GY / VD1943 | Preserved from ST sample | Probe paths are still present and now report trace output during startup. |

### IMX219 Notes

The IMX219 driver stack has two layers that use different pixel-format constants:

- The camera middleware layer uses `CMW_PIXEL_FORMAT_RAW10`.
- The low-level IMX219 component driver expects `IMX219_RAW_RGGB10`.

This fork bridges those values before calling the low-level driver. Without that mapping, the sensor can be detected correctly over I2C but still fail during initialization with an unsupported pixel-format error.

For the tested Arducam module, the expected startup path is:

1. Power and reset are asserted by the board support layer.
2. The camera responds to the IMX219 chip-ID read with `0x0219`.
3. The driver selects the binned `1640x1232 @ 30 fps` mode for the default Nucleo flow.
4. RAW10 output is configured for the CSI/DCMIPP pipeline.
5. USB/UVC enumerates from `CN8` after the application reaches the display pipeline startup.

If the board flashes correctly but no USB camera appears, check the serial trace first. A camera init failure can stop the application before the USB video device ever enumerates.

---

## Tools Version

- [STM32CubeIDE](https://www.st.com/content/st_com/en/products/development-tools/software-development-tools/stm32-software-development-tools/stm32-ides/stm32cubeide.html) (__v1.17.0__)
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) (__v2.19.0__ validated for this fork)
- [STEdgeAI](https://www.st.com/en/development-tools/stedgeai-core.html) (__v4.0.0__)

---

## Boot Modes

The STM32N6 series does not have internal flash memory. To retain firmware after a reboot, program it into the external flash. Alternatively, you can load firmware directly into SRAM (development mode), but note that the program will be lost if the board is powered off in this mode.

Development Mode: used for loading firmware into RAM during a debug session or for programming firmware into external flash.

Boot from Flash: used to boot firmware from external flash.

For NUCLEO-N657X0-Q:

- Boot from flash: ![NUCLEO-N657X0-Q Boot from flash](_htmresc/NUCLEO-N657X0-Q_Boot_from_flash.png)
- Development mode: ![NUCLEO-N657X0-Q Development mode](_htmresc/NUCLEO-N657X0-Q_Dev_mode.png)

---

## Quickstart using stm32ai-modelzoo-services

This application is a C-based project required by the deployment service in the [ModelZoo](https://github.com/STMicroelectronics/stm32ai-modelzoo-services/tree/main). The ModelZoo enables you to train, evaluate, and automatically deploy any supported model.

To deploy your model using the ModelZoo, refer to the [Deployment README for STM32N6](https://github.com/STMicroelectronics/stm32ai-modelzoo-services/blob/main/object_detection/docs/README_DEPLOYMENT_STM32N6.md) for detailed instructions on deploying to the NUCLEO-N657X0-Q.

__Note__: This C-based application is referenced as a submodule of the ModelZoo repository at `application_code/object_detection`.

---

## Quickstart using Prebuilt Binaries

The prebuilt binaries are an assembly of several binaries:
  - FSBL (First Stage Boot Loader, loading the application from flash to RAM)
  - The application
  - The weights of the neural network model

### NUCLEO-N657X0-Q USB/UVC

To program the board's external flash, follow these steps:

1. Set the board to [development mode](#boot-modes).
2. Program `Binary/NUCLEO-N657X0-Q/USB-UVC-Display/NUCLEO-N657X0-Q_GettingStarted_ObjectDetection-uvc.hex`.
3. Set the board to [boot from flash mode](#boot-modes).
4. Connect a USB cable to the USB OTG port (CN8), next to the RJ45 port. Connect the other end to a USB host (PC, USB hub, etc.) for data transmission via USB/UVC.
5. Power cycle the board.
6. Start the camera application on the host. On Windows, search for "camera" in the Start menu.
7. Place a person in front of the camera to detect them.

### NUCLEO-N657X0-Q SPI

To program the board's external flash, follow these steps:

1. Set the board to [development mode](#boot-modes).
2. Program `Binary/NUCLEO-N657X0-Q/SPI-Display/NUCLEO-N657X0-Q_GettingStarted_ObjectDetection-spi.hex`.
3. Set the board to [boot from flash mode](#boot-modes).
4. Power cycle the board.
5. Place a person in front of the camera to detect them.

---

### How to Program Hex Files Using STM32CubeProgrammer UI

See [How to program hex files STM32CubeProgrammer](Doc/Program-Hex-Files-STM32CubeProgrammer.md).

---

### How to Program Hex Files on NUCLEO-N657X0-Q Using Command Line

Ensure the STM32CubeProgrammer `bin` folder is in your PATH.

```bash
export NUEL="<STM32CubeProgrammer_N6 Install Folder>/bin/ExternalLoader/MX25UM51245G_STM32N6570-NUCLEO.stldr"

# USB/UVC
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $NUEL -hardRst -w Binary/NUCLEO-N657X0-Q/USB-UVC-Display/NUCLEO-N657X0-Q_GettingStarted_ObjectDetection-uvc.hex

# SPI
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $NUEL -hardRst -w Binary/NUCLEO-N657X0-Q/SPI-Display/NUCLEO-N657X0-Q_GettingStarted_ObjectDetection-spi.hex
```

---

## Quickstart using Source Code

Before building and running the application, you must program the matching profile's `network_data.hex` (model weights and biases). This only needs to be done once unless you change the AI model. See [Quickstart using prebuilt binaries](#quickstart-using-prebuilt-binaries) for details.

For more information about boot modes, see [Boot Overview](Doc/Boot-Overview.md).

__Note__: To select the NUCLEO-N657X0-Q display interface, use the appropriate build configuration in CubeIDE, or specify `SCR_LIB_SCREEN_ITF=UVCL` or `SCR_LIB_SCREEN_ITF=SPI` as a Makefile option (default is UVCL).

---

### Application Build and Run - Dev Mode

Set your board to [development mode](#boot-modes).

#### STM32CubeIDE

Double-click `Application/<board_name>/STM32CubeIDE/.project` to open the project in STM32CubeIDE. Build and run the project.

#### Makefile

Navigate to `Application/<board_name>/` and run the following commands (ensure required tools are in your PATH):

1. Build the project:
    ```bash
    make -j8
    ```
2. Start a GDB server connected to the STM32 target:
    ```bash
    ST-LINK_gdbserver -p 61234 -l 1 -d -s -cp <path-to-stm32cubeprogrammer-bin-dir> -m 1 -g
    ```
3. In a separate terminal, launch a GDB session to load the firmware:
    ```bash
    $ arm-none-eabi-gdb build/Project.elf
    (gdb) target remote :61234
    (gdb) monitor reset
    (gdb) load
    (gdb) continue
    ```

---

### Application Build and Run - Boot from Flash

Set your board to [development mode](#boot-modes).

#### Build the Application

##### STM32CubeIDE

Double-click `Application/<board_name>/STM32CubeIDE/.project` to open the project in STM32CubeIDE. Build and run the project.

##### Makefile

Ensure all required tools are in your PATH, then build the project:

```bash
make -j8
```

On Windows, you can use the local helper instead of managing the ST tool paths manually:

```powershell
.\build.ps1
```

To select a different local model profile:

```powershell
.\build.ps1 -ModelProfile Generic
.\build.ps1 -ModelProfile SafalObb
```

The helper defaults to a single compile job on Windows for reliability. If your machine handles parallel builds cleanly, you can raise it manually:

```powershell
.\build.ps1 -Jobs 2
```

#### Program the Firmware in the External Flash

After building the application, you must sign the binary file:

```bash
STM32_SigningTool_CLI -bin build/Project.bin -nk -t ssbl -hv 2.3 -o build/Project_sign.bin
```

Program the signed binary at address `0x70100000`, as well as the FSBL and network parameters.

On Windows, the helper can run the build, signing, and flashing sequence for you:

```powershell
.\flash.ps1
```

Recommended Windows workflow for the Nucleo board:

1. Put the board in [development mode](#boot-modes).
2. Connect `CN9` to your PC for ST-LINK access.
3. Pick a model profile and build:
   ```powershell
   .\build.ps1 -ModelProfile Generic
   .\build.ps1 -ModelProfile SafalObb
   ```
4. Sign:
   ```powershell
   .\scripts\stm32n6.ps1 -Action sign -ModelProfile Generic
   .\scripts\stm32n6.ps1 -Action sign -ModelProfile SafalObb
   ```
5. First-time programming for that same profile:
   ```powershell
   .\flash.ps1 -ModelProfile Generic
   .\flash.ps1 -ModelProfile SafalObb
   ```
6. Later application-only updates:
   ```powershell
   .\flash.ps1 -AppOnly -ModelProfile Generic
   .\flash.ps1 -AppOnly -ModelProfile SafalObb
   ```
7. Move the board to [boot from flash](#boot-modes) mode and power-cycle it.
8. For the default Nucleo `UVCL` build, connect `CN8` to your host PC and open a camera viewer.

If you change model profiles or regenerate model artifacts, do a full flash again instead of `-AppOnly`.

Artifacts produced by the Windows helper:

- `Application/<board_name>/build/<ModelProfile>/Project.elf`
- `Application/<board_name>/build/<ModelProfile>/Project.bin`
- `Application/<board_name>/build/<ModelProfile>/Project_sign.bin`

On NUCLEO-N657X0-Q:

```bash
export NUEL="<STM32CubeProgrammer_N6 Install Folder>/bin/ExternalLoader/MX25UM51245G_STM32N6570-NUCLEO.stldr"

# First Stage Boot Loader
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $NUEL -hardRst -w FSBL/ai_fsbl.hex

# Adjust build path as needed
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $NUEL -hardRst -w build/Project_sign.bin 0x70100000

# Network parameters
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $NUEL -hardRst -w Model/NUCLEO-N657X0-Q/network_data.hex
```

__Note__: Only the application binary needs to be programmed if `fsbl` and `network_data.hex` have already been programmed.

Set your board to [boot from flash](#boot-modes) mode and power cycle to boot from external flash.

For `NUCLEO-N657X0-Q` with the default `UVCL` interface, expected behavior after a successful flash is:

1. The board boots from external flash.
2. `CN8` enumerates on the host PC as a USB video device.
3. A camera app on the host PC shows the processed video stream.

If flashing succeeds but you do not get video output, check these first:

1. The board is really in boot-from-flash mode after programming.
2. `FSBL/ai_fsbl.hex` and `Model/<board_name>/network_data.hex` were flashed at least once.
3. You are using `Project_sign.bin`, not the unsigned `Project.bin`.
4. `CN9` is used for ST-LINK flashing and `CN8` is used for the USB camera stream.
5. Your camera hardware path is valid for the selected sensor and interface.

---

## How to update my project with a new version of ST Edge AI

The neural network model files (`network.c/h`, `stai_network.c/h`, etc.) included in this project were generated using [STEdgeAI](https://www.st.com/en/development-tools/stedgeai-core.html) version 4.0.0.

Using a different version of STEdgeAI to generate these model files may result in the following compile-time error:  
`Possible mismatch in ll_aton library used`.

If you encounter this error, please follow the STEdgeAI instructions on [How to update my project with a new version of ST Edge AI Core](https://stedgeai-dc.st.com/assets/embedded-docs/stneuralart_faqs_update_version.html) to update your project.

---

## Known Issues and Limitations

- Only RGB888 format for neural network input has been tested.
- Only UINT8 format for neural network input is supported.
