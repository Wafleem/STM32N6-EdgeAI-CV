<#
.SYNOPSIS
Runs ST Edge AI AiRunner/validate checks against a connected STM32 board.

.DESCRIPTION
This helper wraps the ST Edge AI Core validation tools so we can test the
compiled model on a physical board in a repeatable way.

Important: ST AiRunner requires firmware that includes the aiValidation serial
protocol. The normal camera/UVC object-detection app in this repo does not
expose that protocol, so it will report "Invalid firmware" if that app is
currently flashed. Use this script with an aiValidation-style test firmware.

.EXAMPLE
.\scripts\test-ai-runner-board.ps1

.EXAMPLE
.\scripts\test-ai-runner-board.ps1 -Desc serial:COM16:921600 -BatchSize 4

.EXAMPLE
.\scripts\test-ai-runner-board.ps1 -Action summary -Desc serial:COM16:921600
#>

param(
    [ValidateSet("validate", "summary")]
    [string]$Action = "validate",

    [string]$Model = "Model\bestmerge_320_robomaster_v3_qdq.onnx",

    [string]$Desc = "serial:921600",

    [ValidateSet("target-io-only", "target")]
    [string]$Mode = "target-io-only",

    [int]$BatchSize = 1,

    [string[]]$ValInput = @(),

    [string[]]$ValOutput = @(),

    [string]$StEdgeAiRoot = "",

    [string]$StNeuralArtConfig = "Model\user_neuralart_NUCLEO-N657X0-Q.json",

    [ValidateSet("float32", "int8", "uint8")]
    [string]$InputDataType = "uint8",

    [ValidateSet("float32", "int8", "uint8")]
    [string]$OutputDataType = "int8",

    [ValidateRange(0, 3)]
    [int]$Verbosity = 1,

    [switch]$CompareWithHost,

    [switch]$NoCheck,

    [switch]$DebugRunner
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return (Join-Path $RepoRoot $Path)
}

function Find-STEdgeAiRoot {
    if (-not [string]::IsNullOrWhiteSpace($StEdgeAiRoot)) {
        if (-not (Test-Path -LiteralPath $StEdgeAiRoot -PathType Container)) {
            throw "STEdgeAI root not found: $StEdgeAiRoot"
        }

        return (Resolve-Path -LiteralPath $StEdgeAiRoot).Path
    }

    if ($env:STEDGEAI_CORE_DIR -and (Test-Path -LiteralPath $env:STEDGEAI_CORE_DIR -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:STEDGEAI_CORE_DIR).Path
    }

    $stRoot = "C:\ST\STEdgeAI"
    if (Test-Path -LiteralPath $stRoot -PathType Container) {
        $latest = Get-ChildItem -Path $stRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -First 1

        if ($latest) {
            return $latest.FullName
        }
    }

    throw "Could not find ST Edge AI Core. Install STEdgeAI or pass -StEdgeAiRoot."
}

function Find-Python {
    if ($env:PYTHON -and (Test-Path -LiteralPath $env:PYTHON -PathType Leaf)) {
        return $env:PYTHON
    }

    $candidates = @(
        "C:\Users\saysa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
        "python.exe",
        "python"
    )

    foreach ($candidate in $candidates) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }

        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Could not find Python. Install Python or set the PYTHON environment variable."
}

function Invoke-Captured {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    Write-Host $FilePath $($Arguments -join " ") -ForegroundColor DarkGray
    $output = & $FilePath @Arguments 2>&1
    $exitCode = $LASTEXITCODE

    foreach ($line in $output) {
        Write-Host $line
    }

    if ($exitCode -ne 0) {
        $joined = ($output | Out-String)
        if ($joined -match "Invalid firmware") {
            Write-Host ""
            Write-Host "Hint: AiRunner reached the COM port, but the board is not running aiValidation firmware." -ForegroundColor Yellow
            Write-Host "Flash an ST aiValidation/SystemPerformance-style firmware for this model, then rerun this script." -ForegroundColor Yellow
        } elseif ($joined -match "No SERIAL COM port detected") {
            Write-Host ""
            Write-Host "Hint: no STM32 serial device was detected. Check USB, board power, and -Desc." -ForegroundColor Yellow
        } elseif ($joined -match "PermissionError|Access is denied|could not open port") {
            Write-Host ""
            Write-Host "Hint: another app likely owns the COM port. Close serial monitors/camera tools and retry." -ForegroundColor Yellow
        }

        throw "Command failed ($exitCode): $FilePath $($Arguments -join ' ')"
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$edgeRoot = Find-STEdgeAiRoot
$stedgeaiExe = Join-Path $edgeRoot "Utilities\windows\stedgeai.exe"
$runnerRoot = Join-Path $edgeRoot "scripts\ai_runner"
$resolvedModel = Resolve-RepoPath $Model
$resolvedNeuralArtConfig = Resolve-RepoPath $StNeuralArtConfig

if (-not (Test-Path -LiteralPath $stedgeaiExe -PathType Leaf)) {
    throw "stedgeai.exe not found: $stedgeaiExe"
}

if ($Action -eq "validate") {
    if (-not (Test-Path -LiteralPath $resolvedModel -PathType Leaf)) {
        throw "Model file not found: $resolvedModel"
    }

    if (-not (Test-Path -LiteralPath $resolvedNeuralArtConfig -PathType Leaf)) {
        throw "Neural-ART config not found: $resolvedNeuralArtConfig"
    }
}

Write-Step "ST Edge AI board test"
Write-Host "Action      : $Action"
Write-Host "STEdgeAI    : $edgeRoot"
Write-Host "Descriptor  : $Desc"
if ($Action -eq "validate") {
    Write-Host "Model       : $resolvedModel"
    Write-Host "Mode        : $Mode"
    Write-Host "Batch size  : $BatchSize"
    Write-Host "Host compare: $($CompareWithHost.IsPresent)"
}

if ($Action -eq "summary") {
    if (-not (Test-Path -LiteralPath $runnerRoot -PathType Container)) {
        throw "AiRunner Python package not found: $runnerRoot"
    }

    $python = Find-Python
    $script = @"
import sys
from stm_ai_runner import AiRunner

runner = AiRunner(debug=$($DebugRunner.IsPresent.ToString().ToLowerInvariant()))
runner.connect("$Desc")
if not runner.is_connected:
    print("AiRunner connection failed:", runner.get_error())
    sys.exit(2)
print(runner)
runner.summary()
runner.disconnect()
"@

    $tempScript = Join-Path $env:TEMP ("stm32_ai_runner_summary_{0}.py" -f ([Guid]::NewGuid().ToString("N")))
    Set-Content -LiteralPath $tempScript -Value $script -Encoding UTF8
    try {
        $env:PYTHONPATH = "$runnerRoot;$env:PYTHONPATH"
        Invoke-Captured -FilePath $python -Arguments @($tempScript)
    } finally {
        if (Test-Path -LiteralPath $tempScript -PathType Leaf) {
            Remove-Item -LiteralPath $tempScript -Force
        }
    }

    exit 0
}

$validateArgs = @(
    "validate",
    "--target", "stm32n6",
    "--model", $resolvedModel,
    "--st-neural-art", ("default@{0}" -f $resolvedNeuralArtConfig),
    "--input-data-type", $InputDataType,
    "--output-data-type", $OutputDataType,
    "--mode", $Mode,
    "--desc", $Desc,
    "--batch-size", "$BatchSize",
    "--verbosity", "$Verbosity"
)

if ($Mode -eq "target-io-only") {
    $validateArgs += "--io-only"
}

if (-not $CompareWithHost) {
    $validateArgs += "--no-exec-model"
}

if ($NoCheck) {
    $validateArgs += "--no-check"
}

if ($ValInput.Count -gt 0) {
    $validateArgs += "--valinput"
    foreach ($inputFile in $ValInput) {
        $validateArgs += (Resolve-RepoPath $inputFile)
    }
}

if ($ValOutput.Count -gt 0) {
    $validateArgs += "--valoutput"
    foreach ($outputFile in $ValOutput) {
        $validateArgs += (Resolve-RepoPath $outputFile)
    }
}

Write-Step "Running stedgeai validate"
Invoke-Captured -FilePath $stedgeaiExe -Arguments $validateArgs
