param(
    [Parameter(Mandatory = $true)]
    [string]$Checkpoint,

    [Parameter(Mandatory = $true)]
    [string]$OutputOnnx,

    [int]$ImageSize = 320,

    [int]$Opset = 13,

    [int]$ExpectedClasses = 1,

    [int]$ExpectedBoxes = 2100,

    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$modelDir = Join-Path $repoRoot "Model"
$exportScript = Join-Path $modelDir "export-ultralytics-obb-to-onnx.py"

if (-not (Test-Path -LiteralPath $exportScript -PathType Leaf)) {
    throw "Missing exporter: $exportScript"
}

$checkpointPath = (Resolve-Path -LiteralPath $Checkpoint).Path
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputOnnx)

$pythonPaths = @()
foreach ($candidate in @("evaldeps", "pydeps_onnx", "onnxdeps_local")) {
    $path = Join-Path $modelDir $candidate
    if (Test-Path -LiteralPath $path -PathType Container) {
        $pythonPaths += (Resolve-Path -LiteralPath $path).Path
    }
}

if ($pythonPaths.Count -gt 0) {
    $env:PYTHONPATH = ($pythonPaths -join ";") + $(if ($env:PYTHONPATH) { ";$env:PYTHONPATH" } else { "" })
}

Write-Host "Exporting OBB model:" -ForegroundColor Cyan
Write-Host "  checkpoint : $checkpointPath"
Write-Host "  output     : $outputPath"
Write-Host "  image size : $ImageSize"

& $Python $exportScript $checkpointPath $outputPath `
    --imgsz $ImageSize `
    --opset $Opset `
    --expected-classes $ExpectedClasses `
    --expected-boxes $ExpectedBoxes

if ($LASTEXITCODE -ne 0) {
    throw "ONNX export failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
    throw "Exporter completed but did not create $outputPath"
}

Write-Host "Export complete: $outputPath" -ForegroundColor Green
