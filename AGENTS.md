# Agent Runbook

This repo is easy to lose time in because model conversion, Neural-ART memory
placement, UVC streaming, and MCU-side postprocessing all interact. Follow this
path before inventing a new one.

## Current Known-Good Direction

- Active firmware profile: `SafalObb`.
- Active board model folder: `Model/NUCLEO-N657X0-Q_SafalObb`.
- Best deployable model path: BestMerge OBB at `320x320`, quantized with clean
  RoboMaster v4 images.
- Do not put YOLO/OBB postprocessing, NMS, `TopK`, or selection ops inside the
  ONNX sent to ST Edge AI. The NPU graph must expose the raw YOLO OBB head, and
  the MCU firmware performs decode plus NMS.
- The `384x384` BestMerge clean-v4 quantized model was valid ONNX/QDQ, but ST
  Neural-ART generation failed memory placement with the current `.mpool`.
  Treat `384` as a memory-partition experiment, not the default deploy path.

Expected raw output shapes:

- `320x320`: `[1, 6, 2100]`
- `384x384`: `[1, 6, 3024]`

The channel count is `classes + 5`: one plate class plus OBB box channels.

## Do Not Repeat These Dead Ends

- Do not use a normal Ultralytics exported OBB ONNX if it has output like
  `[1, 300, 7]`. That means export-time postprocessing is still present.
- Do not send ONNX graphs with `NonMaxSuppression`, `TopK`, `Gather`,
  `GatherElements`, `Where`, `Greater`, or similar selection/postprocess ops to
  STAI for this firmware path.
- Do not assume blank UVC means the camera driver is broken. We saw camera
  streaming work with the generic model; custom model failures were caused by
  NPU/model/postprocess integration and memory behavior.
- Do not reinstall Python packages unless strictly necessary. Use the existing
  local dependency path `Model/onnxdeps_local` for ONNX and ONNX Runtime.
- Do not trust Roboflow UI preview boxes as baked-in image pixels. The v4
  download was audited; the UI boxes are overlays, not calibration pixels.
- Do not stage `Model/evaldeps`, `Model/pydeps_onnx`, `Model/onnxdeps_local`, or
  `Model/airunnerdeps`. They are dependency scratch folders and can also cause
  permission warnings during broad git scans.

## Source Model Provenance

The required CV model is:

```text
C:\Users\saysa\Documents\Robomaster_CodeStuff\cv_detection\CV_Detection\model_test\bestmerge.pt
```

Before exporting from it, run `git pull --ff-only` in the CV repo if the user has
asked for the latest CV model. The known BestMerge checkpoint hash used during
this integration was:

```text
2579C18989570995DCBF341FBE51EEE16A8C6EE9323FC9A414D1456ABBB36619
```

## Calibration Dataset Rules

Calibration images should be clean, representative images only. Labels/classes
do not matter for static quantization; the image pixels do.

Preferred current dataset:

```text
C:\Users\saysa\Downloads\RoboMaster.v4-test_robust.yolov8
```

Current clean subset in the repo:

```text
Model/calibration_datasets/robomaster_v4_clean_images
Model/calibration_npy/robomaster_v4_clean_320
```

Audit and select images like this:

```powershell
python Model\audit-calibration-images.py `
  C:\Users\saysa\Downloads\RoboMaster.v4-test_robust.yolov8 `
  --csv Model\robomaster_v4_image_audit.csv

python Model\select-calibration-images.py `
  Model\robomaster_v4_image_audit.csv `
  Model\calibration_datasets\robomaster_v4_clean_images `
  --count 300 `
  --max-block-jump 32 `
  --max-dark 0.12 `
  --max-bright 0.16 `
  --max-saturation 0.22
```

Prepare calibration tensors:

```powershell
python Model\prepare-calibration-npy.py `
  Model\calibration_datasets\robomaster_v4_clean_images `
  Model\calibration_npy\robomaster_v4_clean_320 `
  --imgsz 320 `
  --max-images 300
```

## Raw OBB Verification

Use this verifier before quantization and after quantization:

```powershell
$env:PYTHONPATH=(Resolve-Path 'Model\onnxdeps_local').Path
& 'C:\Users\saysa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
  Model\verify-obb-raw-head.py `
  Model\bestmerge_320.onnx `
  --classes 1 `
  --boxes 2100
```

Success must say the model exposes a raw OBB head and no NMS/TopK/selection ops
were found.

## Quantization Path

For the known-good 320 deploy path:

```powershell
$env:PYTHONPATH=(Resolve-Path 'Model\onnxdeps_local').Path
& 'C:\Users\saysa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
  Model\quantize-obb-onnx.py `
  Model\bestmerge_320.onnx `
  Model\calibration_npy\robomaster_v4_clean_320 `
  Model\bestmerge_320_robomaster_v4_clean_qdq.onnx `
  --int8-boundary-output Model\bestmerge_320_robomaster_v4_clean_uint8in_int8out_qdq.onnx `
  --boundary-input-type uint8 `
  --boundary-output-type int8
```

Then verify both outputs:

```powershell
$env:PYTHONPATH=(Resolve-Path 'Model\onnxdeps_local').Path
python Model\verify-obb-raw-head.py Model\bestmerge_320_robomaster_v4_clean_qdq.onnx --classes 1 --boxes 2100
python Model\verify-obb-raw-head.py Model\bestmerge_320_robomaster_v4_clean_uint8in_int8out_qdq.onnx --classes 1 --boxes 2100
```

## ST Edge AI Generation

Remove stale generated artifacts that would cause overwrite confusion, then run
STAI generation:

```powershell
$stOut = Join-Path (Get-Location) 'st_ai_output'
if (Test-Path $stOut) {
  Get-ChildItem -LiteralPath $stOut -File |
    Where-Object {
      $_.Name -in @('network.c','network_ecblobs.h','stai_network.c','stai_network.h','network_atonbuf.xSPI2.raw') -or
      $_.Name -like 'bestmerge_320_robomaster_v4_clean*'
    } |
    Remove-Item -Force
}

& 'C:\ST\STEdgeAI\4.0\Utilities\windows\stedgeai.exe' generate `
  --model Model\bestmerge_320_robomaster_v4_clean_qdq.onnx `
  --target stm32n6 `
  --st-neural-art default@Model\user_neuralart_NUCLEO-N657X0-Q.json `
  --input-data-type uint8 `
  --output-data-type int8 `
  --verbosity 1
```

Known-good generation summary for the current 320 clean-v4 model:

- Input: `uint8(1x3x320x320)`
- Output: `int8(1x6x2100)`
- MACC: about `695.7M`
- Activations: about `920.8 KiB`
- Weights: about `2.34 MiB`

If generation fails, do not blindly retry. Read the Neural-ART memory placement
error. If it says bytes are left unallocated, the model does not fit the current
memory pool and requires a deliberate `.mpool` plus linker-script partitioning
change.

## Packaging Generated Model Artifacts

After STAI generation succeeds, copy these files into the active Safal OBB model
folder:

```text
st_ai_output/network.c
st_ai_output/network_ecblobs.h
st_ai_output/stai_network.c
st_ai_output/stai_network.h
st_ai_output/network_atonbuf.xSPI2.raw
```

The raw blob must become:

```text
Model/NUCLEO-N657X0-Q_SafalObb/network_data.xSPI2.bin
Model/NUCLEO-N657X0-Q_SafalObb/network_data.hex
```

Generate the hex from the raw blob with `arm-none-eabi-objcopy`:

```powershell
arm-none-eabi-objcopy `
  -I binary Model\NUCLEO-N657X0-Q_SafalObb\network_data.xSPI2.bin `
  --change-addresses 0x70380000 `
  -O ihex Model\NUCLEO-N657X0-Q_SafalObb\network_data.hex
```

Verify the packaged header before building:

```powershell
Select-String -Path Model\NUCLEO-N657X0-Q_SafalObb\stai_network.h `
  -Pattern 'ORIGIN_MODEL_NAME|IN_1_HEIGHT|IN_1_WIDTH|OUT_1_HEIGHT|OUT_1_WIDTH|OUT_1_SIZE_BYTES'
```

For 320 clean-v4 it should name:

```text
bestmerge_320_robomaster_v4_clean_qdq_OE_3_3_1
```

## Firmware Config

Keep the Safal OBB section in:

```text
Application/NUCLEO-N657X0-Q/Inc/app_config.h
```

Current expected values:

```c
#define POSTPROCESS_TYPE POSTPROCESS_CUSTOM
#define APP_MODEL_PROFILE_NAME "BestMerge OBB 320"
#define APP_MODEL_CALIBRATION_NAME "RoboMaster v4 clean 320 calibration"
#define NB_CLASSES 1
#define AI_OD_OBB_PP_TOTAL_BOXES (2100)
#define AI_OD_OBB_PP_OUTPUT_IS_RAW_YOLO26 (1U)
```

If the model size changes, update `AI_OD_OBB_PP_TOTAL_BOXES` to match the raw
head width from `stai_network.h`. Do not guess this value.

## Build, Sign, Flash

Clean build:

```powershell
.\scripts\stm32n6.ps1 -Action build -ModelProfile SafalObb -Jobs 2 -Clean
```

Sign regenerated app image:

```powershell
.\scripts\stm32n6.ps1 -Action sign -ModelProfile SafalObb -Jobs 2
```

Flash all when the board is connected and in the correct mode:

```powershell
.\scripts\stm32n6.ps1 -Action flash-all -ModelProfile SafalObb -Jobs 2
```

`flash-all` programs:

- `FSBL/ai_fsbl.hex`
- `Model/NUCLEO-N657X0-Q_SafalObb/network_data.hex`
- the signed application image

After flashing, switch to boot-from-flash mode and power-cycle if the script says
to do so.

## AI Runner Validation Inputs

ST AI Runner / `stedgeai validate` sends preprocessed tensors to the board, not
raw `.jpg` files. For the current BestMerge 320 model, validation input files
should contain flattened `uint8` rows:

```text
shape=(N, 307200)
dtype=uint8
layout=RGB CHW flattened
preprocess=square-pad/letterbox with 114, resize 320x320
```

Generate a small image batch with:

```powershell
python Model\prepare-validation-inputs.py `
  Model\calibration_datasets\robomaster_v4_clean_images `
  Model\validation_inputs\bestmerge_v4_uint8_320_n10.npy `
  --manifest Model\validation_inputs\bestmerge_v4_uint8_320_n10.csv `
  --imgsz 320 `
  --count 10
```

Validate with:

```powershell
.\scripts\test-ai-runner-board.ps1 `
  -Action validate `
  -Desc serial:COM16:921600 `
  -ValInput Model\validation_inputs\bestmerge_v4_uint8_320_n10.npy `
  -BatchSize 10
```

## Expected Runtime Banner

After flashing the current path, serial output should show:

```text
NN model: bestmerge_320_robomaster_v4_clean_qdq_OE_3_3_1
Model profile: BestMerge OBB 320
Calibration: RoboMaster v4 clean 320 calibration
```

Runtime should show camera init, UVC init, display pipe start, NN pipe start, and
repeated inference completions. Previous working inference was roughly
`80-100 ms` per frame after NPU-side postprocessing was removed.

## Debugging Priorities

If UVC is blank:

- First confirm the generic model still streams.
- Then confirm the custom model has no NPU-side postprocess ops.
- Then inspect STAI generation memory usage and packaged `network_data.hex`.
- Only then suspect camera driver changes.

If inference times out or hardfaults:

- Check raw output shape and `AI_OD_OBB_PP_TOTAL_BOXES`.
- Check that `network.c`, `stai_network.*`, `network_ecblobs.h`, and
  `network_data.hex` are from the same STAI generation run.
- Check Neural-ART `.mpool` and linker-script memory partitioning for overlap.

If detections are missing but inference runs:

- Confirm the calibration images are clean and representative.
- Lower or log `AI_OD_OBB_PP_CONF_THRESHOLD`.
- Add postprocess traces for top raw confidences before NMS.
- Compare float ONNX and quantized ONNX outputs on the same still images before
  blaming camera/UVC.

## Git Hygiene

- Keep generated deploy artifacts together in one checkpoint: ONNX/QDQ model,
  selected calibration metadata, generated `Model/NUCLEO-N657X0-Q_SafalObb`
  files, and firmware config changes.
- Do not commit dependency folders or temporary export work dirs.
- Before committing, run:

```powershell
git status --short
```

Review untracked debug images/logs and only add the ones that are intentionally
part of the handoff.
