param(
    [Parameter(Mandatory = $true)]
    [string]$ModelOnnx,

    [string]$OutputProfileDir = "Model\NUCLEO-N657X0-Q_SafalObb",

    [string]$NeuralArtConfig = "Model\user_neuralart_NUCLEO-N657X0-Q.json",

    [string]$StEdgeAi = "C:\ST\STEdgeAI\4.0\Utilities\windows\stedgeai.exe",

    [string]$Objcopy = "arm-none-eabi-objcopy",

    [string]$WeightsAddress = "0x70380000"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$modelPath = (Resolve-Path -LiteralPath $ModelOnnx).Path
$outputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputProfileDir)
$neuralArtPath = (Resolve-Path -LiteralPath $NeuralArtConfig).Path

if (-not (Test-Path -LiteralPath $StEdgeAi -PathType Leaf)) {
    $command = Get-Command stedgeai -ErrorAction SilentlyContinue
    if ($command) {
        $StEdgeAi = $command.Source
    } else {
        throw "Could not find stedgeai. Pass -StEdgeAi or install ST Edge AI."
    }
}

$objcopyCommand = Get-Command $Objcopy -ErrorAction SilentlyContinue
if (-not $objcopyCommand) {
    $candidate = Get-ChildItem -Path "C:\ST" -Recurse -Filter "arm-none-eabi-objcopy.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($candidate) {
        $Objcopy = $candidate.FullName
    } else {
        throw "Could not find arm-none-eabi-objcopy. Pass -Objcopy or install STM32CubeIDE."
    }
} else {
    $Objcopy = $objcopyCommand.Source
}

Push-Location (Join-Path $repoRoot "Model")
try {
    Write-Host "Generating STM32N6 artifacts:" -ForegroundColor Cyan
    Write-Host "  model  : $modelPath"
    Write-Host "  output : $outputDir"

    & $StEdgeAi generate `
        --model $modelPath `
        --target stm32n6 `
        --st-neural-art "default@$neuralArtPath" `
        --input-data-type uint8 `
        --output-data-type int8 `
        --verbosity 1

    $stEdgeCode = $LASTEXITCODE
    $required = @(
        "st_ai_output\network.c",
        "st_ai_output\network_atonbuf.xSPI2.raw",
        "st_ai_output\stai_network.c",
        "st_ai_output\stai_network.h",
        "st_ai_output\network_ecblobs.h"
    )
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })

    if (($stEdgeCode -ne 0) -and ($missing.Count -gt 0)) {
        throw "stedgeai failed with exit $stEdgeCode and missing outputs: $($missing -join ', ')"
    }

    if ($stEdgeCode -ne 0) {
        Write-Warning "stedgeai returned $stEdgeCode but generated usable artifacts; packaging them."
    }

    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    Copy-Item st_ai_output\network.c (Join-Path $outputDir "network.c") -Force
    Copy-Item st_ai_output\network_ecblobs.h (Join-Path $outputDir "network_ecblobs.h") -Force
    Copy-Item st_ai_output\stai_network.c (Join-Path $outputDir "stai_network.c") -Force
    Copy-Item st_ai_output\stai_network.h (Join-Path $outputDir "stai_network.h") -Force
    Copy-Item st_ai_output\network_atonbuf.xSPI2.raw (Join-Path $outputDir "network_data.xSPI2.bin") -Force

    & $Objcopy -I binary `
        (Join-Path $outputDir "network_data.xSPI2.bin") `
        --change-addresses $WeightsAddress `
        -O ihex `
        (Join-Path $outputDir "network_data.hex")

    if ($LASTEXITCODE -ne 0) {
        throw "objcopy failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host "Generated artifacts in $outputDir" -ForegroundColor Green
