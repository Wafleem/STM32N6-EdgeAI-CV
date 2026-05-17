param(
    [Parameter(Mandatory = $true)]
    [string]$InputOnnx,

    [string]$CalibrationImages,

    [string]$CalibrationTensors,

    [Parameter(Mandatory = $true)]
    [string]$QdqOutput,

    [string]$BoundaryOutput,

    [int]$ImageSize = 320,

    [int]$MaxImages = 192,

    [string]$BoundaryInputType = "uint8",

    [string]$BoundaryOutputType = "int8",

    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$modelDir = Join-Path $repoRoot "Model"
$prepareScript = Join-Path $modelDir "prepare-calibration-npy.py"
$quantizeScript = Join-Path $modelDir "quantize-obb-onnx.py"

if (-not (Test-Path -LiteralPath $quantizeScript -PathType Leaf)) {
    throw "Missing quantizer: $quantizeScript"
}

if ([string]::IsNullOrWhiteSpace($CalibrationImages) -and [string]::IsNullOrWhiteSpace($CalibrationTensors)) {
    throw "Provide either -CalibrationImages or -CalibrationTensors."
}

$pythonPaths = @()
foreach ($candidate in @("pydeps_onnx", "evaldeps", "onnxdeps_local")) {
    $path = Join-Path $modelDir $candidate
    if (Test-Path -LiteralPath $path -PathType Container) {
        $pythonPaths += (Resolve-Path -LiteralPath $path).Path
    }
}

if ($pythonPaths.Count -gt 0) {
    $env:PYTHONPATH = ($pythonPaths -join ";") + $(if ($env:PYTHONPATH) { ";$env:PYTHONPATH" } else { "" })
}

$inputPath = (Resolve-Path -LiteralPath $InputOnnx).Path
$qdqPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($QdqOutput)
$calibrationDir = $null

if (-not [string]::IsNullOrWhiteSpace($CalibrationTensors)) {
    $calibrationDir = (Resolve-Path -LiteralPath $CalibrationTensors).Path
} else {
    if (-not (Test-Path -LiteralPath $prepareScript -PathType Leaf)) {
        throw "Missing calibration preparer: $prepareScript"
    }

    $imagesPath = (Resolve-Path -LiteralPath $CalibrationImages).Path
    $calibrationDir = Join-Path $modelDir ("calibration_npy\generated_{0}" -f $ImageSize)

    Write-Host "Preparing calibration tensors:" -ForegroundColor Cyan
    Write-Host "  images : $imagesPath"
    Write-Host "  output : $calibrationDir"

    & $Python $prepareScript $imagesPath $calibrationDir --imgsz $ImageSize --max-images $MaxImages
    if ($LASTEXITCODE -ne 0) {
        throw "Calibration tensor preparation failed with exit code $LASTEXITCODE"
    }
}

$args = @($quantizeScript, $inputPath, $calibrationDir, $qdqPath)

if (-not [string]::IsNullOrWhiteSpace($BoundaryOutput)) {
    $boundaryPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BoundaryOutput)
    $args += @(
        "--int8-boundary-output", $boundaryPath,
        "--boundary-input-type", $BoundaryInputType,
        "--boundary-output-type", $BoundaryOutputType
    )
}

Write-Host "Quantizing OBB ONNX:" -ForegroundColor Cyan
Write-Host "  input       : $inputPath"
Write-Host "  calibration : $calibrationDir"
Write-Host "  qdq output  : $qdqPath"

& $Python @args
if ($LASTEXITCODE -ne 0) {
    throw "Quantization failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $qdqPath -PathType Leaf)) {
    throw "Quantizer completed but did not create $qdqPath"
}

Write-Host "Quantization complete: $qdqPath" -ForegroundColor Green
