param(
    [string]$SourceCheckpoint = "..\..\cv_detection\CV_Detection\model_test\bestmerge.pt",
    [int]$ImgSize = 384,
    [int]$Classes = 1,
    [string]$Python = "C:\Users\saysa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
    [string]$PythonDeps = "C:\tmp\stm32n6_yolo_deps",
    [switch]$RunStAnalyze
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
    }
}

$modelDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $modelDir "..")).Path
$source = (Resolve-Path (Join-Path $repoRoot $SourceCheckpoint)).Path
$workDir = Join-Path $modelDir "export_work"
$workCheckpoint = Join-Path $workDir "bestmerge.pt"
$output = Join-Path $modelDir ("bestmerge_{0}_raw_yolo.onnx" -f $ImgSize)
$boxes = [int]((($ImgSize / 8) * ($ImgSize / 8)) + (($ImgSize / 16) * ($ImgSize / 16)) + (($ImgSize / 32) * ($ImgSize / 32)))

Write-Step "Preparing BestMerge checkpoint"
New-Item -ItemType Directory -Force $workDir | Out-Null
Copy-Item -LiteralPath $source -Destination $workCheckpoint -Force
Get-FileHash -Algorithm SHA256 $source, $workCheckpoint | Format-Table Path, Hash -AutoSize

Write-Step "Exporting raw YOLO OBB head"
if (Test-Path -LiteralPath $PythonDeps -PathType Container) {
    $env:PYTHONPATH = $PythonDeps
}
$env:YOLO_CONFIG_DIR = Join-Path $workDir "ultralytics_config"

Invoke-Checked -FilePath $Python -Arguments @(
    (Join-Path $modelDir "export-ultralytics-obb-to-onnx.py"),
    $workCheckpoint,
    $output,
    "--imgsz", [string]$ImgSize,
    "--opset", "13",
    "--expected-classes", [string]$Classes,
    "--expected-boxes", [string]$boxes
)

Write-Step "Verifying raw head and forbidden postprocess ops"
Invoke-Checked -FilePath $Python -Arguments @(
    (Join-Path $modelDir "verify-obb-raw-head.py"),
    $output,
    "--classes", [string]$Classes,
    "--boxes", [string]$boxes
)

if ($RunStAnalyze) {
    Write-Step "Running ST Edge AI analyze on raw float ONNX"
    $stedgeai = "C:\ST\STEdgeAI\4.0\Utilities\windows\stedgeai.exe"
    Invoke-Checked -FilePath $stedgeai -Arguments @(
        "analyze",
        "--model", $output,
        "--target", "stm32n6",
        "--st-neural-art", ("default@" + (Join-Path $modelDir "user_neuralart_NUCLEO-N657X0-Q.json")),
        "--verbosity", "1"
    )
}

Write-Step "Done"
Write-Host $output
