param(
    [ValidateSet("NUCLEO-N657X0-Q")]
    [string]$Board = "NUCLEO-N657X0-Q",

    [ValidateSet("build", "sign", "flash-app", "flash-all")]
    [string]$Action = "build",

    [ValidateSet("UVCL", "SPI")]
    [string]$Interface = "UVCL",

    [ValidateSet("Generic", "SafalObb")]
    [string]$ModelProfile = "Generic",

    [int]$Jobs = 1,

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Get-FirstExistingFile {
    param(
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Get-CommandPath {
    param(
        [string[]]$Names
    )

    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    return $null
}

function Find-LatestMatchingFile {
    param(
        [string]$Root,
        [string]$Filter
    )

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        return $null
    }

    $match = Get-ChildItem -Path $Root -Recurse -File -Filter $Filter -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if ($match) {
        return $match.FullName
    }

    return $null
}

function Get-CubeIdeTool {
    param([string]$Executable)

    $stRoot = "C:\ST"
    if (-not (Test-Path -LiteralPath $stRoot -PathType Container)) {
        return $null
    }

    $ideRoots = Get-ChildItem -Path $stRoot -Directory -Filter "STM32CubeIDE*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending

    foreach ($ideRoot in $ideRoots) {
        $pluginRoot = Join-Path $ideRoot.FullName "STM32CubeIDE\plugins"
        $tool = Find-LatestMatchingFile -Root $pluginRoot -Filter $Executable
        if ($tool) {
            return $tool
        }
    }

    return $null
}

function Get-RequiredTool {
    param(
        [string]$DisplayName,
        [string[]]$CommandNames,
        [string[]]$CandidateFiles = @(),
        [switch]$SearchCubeIde
    )

    $fromPath = Get-CommandPath -Names $CommandNames
    if ($fromPath) {
        return $fromPath
    }

    $candidate = Get-FirstExistingFile -Candidates $CandidateFiles
    if ($candidate) {
        return $candidate
    }

    if ($SearchCubeIde) {
        foreach ($name in $CommandNames) {
            $tool = Get-CubeIdeTool -Executable $name
            if ($tool) {
                return $tool
            }
        }
    }

    throw "Could not find $DisplayName. Install STM32CubeIDE / STM32CubeProgrammer, or add the tool to PATH."
}

function Get-PreferredCubeIdeTool {
    param(
        [string]$DisplayName,
        [string[]]$CommandNames
    )

    foreach ($name in $CommandNames) {
        $tool = Get-CubeIdeTool -Executable $name
        if ($tool) {
            return $tool
        }
    }

    return Get-RequiredTool -DisplayName $DisplayName -CommandNames $CommandNames
}

function Invoke-External {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
    }
}

function Add-PathEntry {
    param([string]$PathEntry)

    if ([string]::IsNullOrWhiteSpace($PathEntry)) {
        return
    }

    $parts = $env:Path -split ';'
    if ($parts -notcontains $PathEntry) {
        $env:Path = "$PathEntry;$env:Path"
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$appDir = Join-Path $repoRoot ("Application\" + $Board)
$buildDir = Join-Path $appDir ("build\" + $ModelProfile)
$projectBin = Join-Path $buildDir "Project.bin"
$projectElf = Join-Path $buildDir "Project.elf"
$signedBin = Join-Path $buildDir "Project_sign.bin"
$fsblHex = Join-Path $repoRoot "FSBL\ai_fsbl.hex"
$modelDirName = if ($ModelProfile -eq "SafalObb") {
    "{0}_SafalObb" -f $Board
} else {
    $Board
}
$modelDir = Join-Path $repoRoot ("Model\" + $modelDirName)
$networkHex = Join-Path $modelDir "network_data.hex"

if (-not (Test-Path -LiteralPath $appDir -PathType Container)) {
    throw "Unsupported board path: $appDir"
}

if (-not (Test-Path -LiteralPath $modelDir -PathType Container)) {
    throw "Model artifacts not found for profile '$ModelProfile': $modelDir"
}

$makeExe = Get-PreferredCubeIdeTool -DisplayName "GNU make" -CommandNames @("make.exe", "make")
$gccExe = Get-PreferredCubeIdeTool -DisplayName "arm-none-eabi-gcc" -CommandNames @("arm-none-eabi-gcc.exe", "arm-none-eabi-gcc")
$shExe = Get-PreferredCubeIdeTool -DisplayName "sh.exe" -CommandNames @("sh.exe", "sh")

Add-PathEntry -PathEntry (Split-Path -Parent $makeExe)
Add-PathEntry -PathEntry (Split-Path -Parent $gccExe)
Add-PathEntry -PathEntry (Split-Path -Parent $shExe)
$env:SHELL = $shExe

$programmerExe = $null
$signingExe = $null
$externalLoader = $null

if ($Action -in @("sign", "flash-app", "flash-all")) {
    $programmerBin = Split-Path -Parent (Get-RequiredTool -DisplayName "STM32CubeProgrammer CLI" `
        -CommandNames @("STM32_Programmer_CLI.exe", "STM32_Programmer_CLI") `
        -CandidateFiles @("C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"))

    $programmerExe = Join-Path $programmerBin "STM32_Programmer_CLI.exe"
    $signingExe = Get-RequiredTool -DisplayName "STM32 signing tool" `
        -CommandNames @("STM32_SigningTool_CLI.exe", "STM32_SigningTool_CLI") `
        -CandidateFiles @((Join-Path $programmerBin "STM32_SigningTool_CLI.exe"))

    $loaderName = "MX25UM51245G_STM32N6570-NUCLEO.stldr"
    $externalLoader = Join-Path $programmerBin ("ExternalLoader\" + $loaderName)
    if (-not (Test-Path -LiteralPath $externalLoader -PathType Leaf)) {
        throw "Could not find external loader: $externalLoader"
    }
}

function Invoke-Build {
    if ($Clean) {
        Write-Step "Cleaning $Board"
        Invoke-External -FilePath $makeExe -Arguments @("-C", $appDir, "clean")
    }

    Write-Step "Building $Board ($Interface)"
    $makeArgs = @("-C", $appDir, ("-j{0}" -f $Jobs))

    $makeArgs += ("SCR_LIB_SCREEN_ITF={0}" -f $Interface)

    $profileValue = if ($ModelProfile -eq "Generic") { 0 } else { 1 }
    $makeArgs += ("APP_MODEL_PROFILE={0}" -f $profileValue)

    Invoke-External -FilePath $makeExe -Arguments $makeArgs

    if (-not (Test-Path -LiteralPath $projectElf -PathType Leaf)) {
        throw "Build completed but $projectElf was not produced."
    }

    if (-not (Test-Path -LiteralPath $projectBin -PathType Leaf)) {
        throw "Build completed but $projectBin was not produced."
    }
}

function Invoke-Sign {
    if (-not (Test-Path -LiteralPath $projectBin -PathType Leaf)) {
        throw "Binary not found: $projectBin"
    }

    Write-Step "Signing application image"
    Invoke-External -FilePath $signingExe -Arguments @(
        "-bin", $projectBin,
        "-nk",
        "-t", "ssbl",
        "-hv", "2.3",
        "-o", $signedBin
    )

    if (-not (Test-Path -LiteralPath $signedBin -PathType Leaf)) {
        throw "Signing completed but $signedBin was not produced."
    }
}

function Invoke-FlashFile {
    param(
        [string]$FilePath,
        [string]$Address
    )

    if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
        throw "Flash file not found: $FilePath"
    }

    $args = @("-c", "port=SWD", "mode=HOTPLUG", "-el", $externalLoader, "-hardRst", "-w", $FilePath)
    if ($Address) {
        $args += $Address
    }

    Invoke-External -FilePath $programmerExe -Arguments $args
}

function Invoke-FlashApp {
    Write-Step "Flashing signed application"
    Invoke-FlashFile -FilePath $signedBin -Address "0x70100000"
}

function Invoke-FlashAll {
    Write-Step "Flashing FSBL"
    Invoke-FlashFile -FilePath $fsblHex -Address ""

    Write-Step "Flashing model weights"
    Invoke-FlashFile -FilePath $networkHex -Address ""

    Invoke-FlashApp
}

Invoke-Build

switch ($Action) {
    "build" {
        Write-Step "Done"
        Write-Host "ELF : $projectElf"
        Write-Host "BIN : $projectBin"
    }
    "sign" {
        Invoke-Sign
        Write-Step "Done"
        Write-Host "SIGNED BIN : $signedBin"
    }
    "flash-app" {
        Invoke-Sign
        Invoke-FlashApp
        Write-Step "Done"
        Write-Host "Flashed app image to 0x70100000"
    }
    "flash-all" {
        Invoke-Sign
        Invoke-FlashAll
        Write-Step "Done"
        Write-Host "Flashed FSBL, network_data, and signed app."
        Write-Host "Switch the board to boot-from-flash mode, then power-cycle it."
    }
}
