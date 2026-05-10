param(
    [string]$Python = "C:\Users\saysa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
    [string]$Deps = "Model\onnxdeps_local",
    [int]$ImgSize = 224
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$modelDir = $PSScriptRoot
$calibrationImages = Join-Path $modelDir "calibration_datasets\robomaster_v3_test200_obb\images\val"
$calibrationNpy = Join-Path $modelDir ("calibration_npy\robomaster_v3_test200_{0}" -f $ImgSize)
$inputModel = Join-Path $modelDir ("nitish_red_blue_obb_{0}.onnx" -f $ImgSize)
$qdqModel = Join-Path $modelDir ("nitish_red_blue_obb_{0}_robomaster_v3_qdq.onnx" -f $ImgSize)
$boundaryModel = Join-Path $modelDir ("nitish_red_blue_obb_{0}_robomaster_v3_uint8in_int8out_qdq.onnx" -f $ImgSize)
$depsPath = Resolve-Path (Join-Path $repoRoot $Deps)

& $Python (Join-Path $modelDir "prepare-calibration-npy.py") `
    $calibrationImages `
    $calibrationNpy `
    --imgsz $ImgSize `
    --max-images 200

$env:PYTHONPATH = $depsPath.Path
& $Python (Join-Path $modelDir "quantize-obb-onnx.py") `
    $inputModel `
    $calibrationNpy `
    $qdqModel `
    --int8-boundary-output $boundaryModel `
    --boundary-input-type uint8 `
    --boundary-output-type int8
