<#
.SYNOPSIS
Prepares, builds, and optionally flashes ST's NPU aiValidation firmware.

.DESCRIPTION
AiRunner talks to the board through ST's aiValidation protocol, not through
our normal camera/UVC firmware. This helper creates a temporary validation
workspace from the installed ST EdgeAI NPU_Validation project, injects this
repo's generated BestMerge model artifacts, and builds a NUCLEO-N657X0-Q
validation image.

The generated validation app is RAM-linked. Flashing this helper loads the
model weights to external flash and then loads/runs the validation app in SRAM.

.EXAMPLE
.\scripts\stm32n6-ai-validation.ps1 -Action all

.EXAMPLE
.\scripts\stm32n6-ai-validation.ps1 -Action flash

.EXAMPLE
.\scripts\stm32n6-ai-validation.ps1 -Action validate -Desc serial:COM16:921600
#>

param(
    [ValidateSet("prepare", "build", "flash", "validate", "all")]
    [string]$Action = "all",

    [ValidateSet("N6-Nucleo", "N6-Nucleo-USB")]
    [string]$BuildConf = "N6-Nucleo",

    [string]$StEdgeAiRoot = "",

    [string]$ValidationRoot = "C:\tmp\stm32n6-ai-validation",

    [string]$ModelArtifacts = "Model\NUCLEO-N657X0-Q_SafalObb",

    [string]$NetworkHex = "Model\NUCLEO-N657X0-Q_SafalObb\network_data.hex",

    [string]$Desc = "serial:921600",

    [ValidateSet("HOTPLUG", "UR")]
    [string]$ConnectMode = "UR",

    [int]$Jobs = 2,

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Get-FirstExistingFile {
    param([string[]]$Candidates)

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
    param([string[]]$Names)

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

function Invoke-External {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    Write-Host $FilePath $($Arguments -join " ") -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
    }
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
            Where-Object {
                (Test-Path -LiteralPath (Join-Path $_.FullName "Utilities\windows\stedgeai.exe") -PathType Leaf) -and
                (Test-Path -LiteralPath (Join-Path $_.FullName "Projects\STM32N6570-DK\Applications\NPU_Validation") -PathType Container)
            } |
            Sort-Object Name -Descending |
            Select-Object -First 1

        if ($latest) {
            return $latest.FullName
        }
    }

    throw "Could not find ST EdgeAI. Install it or pass -StEdgeAiRoot."
}

function Convert-ToMakePath {
    param([string]$Path)
    return $Path.Replace("\", "/")
}

function Get-ValidationPaths {
    $projectRoot = Join-Path $ValidationRoot "Projects\STM32N6570-DK"
    $appRoot = Join-Path $projectRoot "Applications\NPU_Validation"
    $armgccRoot = Join-Path $appRoot "armgcc"
    $buildRoot = Join-Path $armgccRoot ("build\" + $BuildConf)

    return [pscustomobject]@{
        ProjectRoot = $projectRoot
        AppRoot = $appRoot
        ArmGccRoot = $armgccRoot
        BuildRoot = $buildRoot
        ProjectHex = Join-Path $buildRoot "Project.hex"
        ProjectBin = Join-Path $buildRoot "Project.bin"
        ProjectElf = Join-Path $buildRoot "Project.elf"
        XCubeApp = Join-Path $appRoot "X-CUBE-AI\App"
    }
}

function Invoke-Prepare {
    $paths = Get-ValidationPaths
    $srcProjectRoot = Join-Path $EdgeRoot "Projects\STM32N6570-DK"
    $srcValidationApp = Join-Path $srcProjectRoot "Applications\NPU_Validation"
    $srcDrivers = Join-Path $srcProjectRoot "Applications\Drivers"
    $srcMiddlewares = Join-Path $EdgeRoot "Middlewares\ST"
    $resolvedArtifacts = Resolve-RepoPath $ModelArtifacts

    foreach ($required in @($srcValidationApp, $srcDrivers, $srcMiddlewares, $resolvedArtifacts)) {
        if (-not (Test-Path -LiteralPath $required -PathType Container)) {
            throw "Required directory not found: $required"
        }
    }

    if ($Clean -and (Test-Path -LiteralPath $ValidationRoot -PathType Container)) {
        Write-Step "Cleaning validation workspace"
        Remove-Item -LiteralPath $ValidationRoot -Recurse -Force
    }

    if (-not (Test-Path -LiteralPath $paths.AppRoot -PathType Container)) {
        Write-Step "Creating validation workspace"
        New-Item -ItemType Directory -Path $ValidationRoot -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $ValidationRoot "Projects") -Force | Out-Null
        New-Item -ItemType Directory -Path $paths.ProjectRoot -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $paths.ProjectRoot "Applications") -Force | Out-Null
        Copy-Item -LiteralPath $srcValidationApp -Destination (Join-Path $paths.ProjectRoot "Applications") -Recurse -Force
        Copy-Item -LiteralPath $srcDrivers -Destination (Join-Path $paths.ProjectRoot "Applications") -Recurse -Force
    }

    Write-Step "Injecting generated BestMerge model artifacts"
    foreach ($file in @("network.c", "network_ecblobs.h", "stai_network.c", "stai_network.h")) {
        $src = Join-Path $resolvedArtifacts $file
        $dst = Join-Path $paths.XCubeApp $file

        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
            throw "Generated model artifact not found: $src"
        }

        Copy-Item -LiteralPath $src -Destination $dst -Force
    }

    $makefile = Join-Path $paths.ArmGccRoot "Makefile"
    $content = Get-Content -LiteralPath $makefile -Raw
    $middlewaresMakePath = Convert-ToMakePath $srcMiddlewares
    $atonMakePath = Convert-ToMakePath (Join-Path $srcMiddlewares "AI\Npu\ll_aton")

    $content = $content -replace '(?m)^MIDDLEWARES_PATH\s*=.*$', "MIDDLEWARES_PATH = $middlewaresMakePath"
    $content = $content -replace '(?m)^ATON_RT_PATH\s*=.*$', "ATON_RT_PATH = $atonMakePath"
    $content = $content -replace '(?m)^LIBDIR\s*=.*$', "LIBDIR = $middlewaresMakePath/AI/Lib/GCC/ARMCortexM55"
    $content = $content -replace 'ATON_SOURCES \+= \$\(ATON_RT_PATH\)/ll_aton_runtime\.c', "ATON_SOURCES += `$`(ATON_RT_PATH`)/ll_aton_runtime.c`r`nATON_SOURCES += `$`(ATON_RT_PATH`)/ll_aton_stai_internal.c"
    $content = $content -replace 'VALIDATION_SOURCES \+= \$\(VALIDATION_PATH\)/network\.c', "VALIDATION_SOURCES += `$`(VALIDATION_PATH`)/network.c`r`nVALIDATION_SOURCES += `$`(VALIDATION_PATH`)/stai_network.c"
    $content = $content -replace 'AI/Validation/Src/ai_wrapper_ATON\.c', 'AI/Validation/Src/ai_wrapper_ATON_ST_AI.c'
    $content = $content -replace 'AI/Validation/Src/aiValidation_ATON\.c', 'AI/Validation/Src/aiValidation_ATON_ST_AI.c'

    Set-Content -LiteralPath $makefile -Value $content -Encoding ASCII

    Write-Step "Validation workspace ready"
    Write-Host "Workspace : $($paths.AppRoot)"
    Write-Host "Build conf: $BuildConf"
}

function Invoke-Build {
    $paths = Get-ValidationPaths
    if (-not (Test-Path -LiteralPath $paths.ArmGccRoot -PathType Container)) {
        Invoke-Prepare
    }

    $makeExe = Get-PreferredCubeIdeTool -DisplayName "GNU make" -CommandNames @("make.exe", "make")
    $gccExe = Get-PreferredCubeIdeTool -DisplayName "arm-none-eabi-gcc" -CommandNames @("arm-none-eabi-gcc.exe", "arm-none-eabi-gcc")
    $shExe = Get-PreferredCubeIdeTool -DisplayName "sh.exe" -CommandNames @("sh.exe", "sh")

    Add-PathEntry -PathEntry (Split-Path -Parent $makeExe)
    Add-PathEntry -PathEntry (Split-Path -Parent $gccExe)
    Add-PathEntry -PathEntry (Split-Path -Parent $shExe)
    $env:SHELL = $shExe

    if ($Clean -and (Test-Path -LiteralPath $paths.BuildRoot -PathType Container)) {
        Write-Step "Removing old validation build"
        Remove-Item -LiteralPath $paths.BuildRoot -Recurse -Force
    }

    Write-Step "Building aiValidation firmware"
    Invoke-External -FilePath $makeExe -Arguments @(
        "-C", $paths.ArmGccRoot,
        ("BUILD_CONF={0}" -f $BuildConf),
        ("GCC_PATH={0}" -f (Convert-ToMakePath (Split-Path -Parent $gccExe))),
        ("-j{0}" -f $Jobs)
    )

    foreach ($output in @($paths.ProjectElf, $paths.ProjectHex, $paths.ProjectBin)) {
        if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "Validation build completed but output is missing: $output"
        }
    }

    Write-Step "Validation firmware built"
    Write-Host "ELF : $($paths.ProjectElf)"
    Write-Host "HEX : $($paths.ProjectHex)"
    Write-Host "BIN : $($paths.ProjectBin)"
}

function Invoke-Flash {
    $paths = Get-ValidationPaths
    $resolvedNetworkHex = Resolve-RepoPath $NetworkHex

    if (-not (Test-Path -LiteralPath $paths.ProjectHex -PathType Leaf)) {
        Invoke-Build
    }

    if (-not (Test-Path -LiteralPath $resolvedNetworkHex -PathType Leaf)) {
        throw "Network HEX not found: $resolvedNetworkHex"
    }

    $programmerBin = Split-Path -Parent (Get-RequiredTool -DisplayName "STM32CubeProgrammer CLI" `
        -CommandNames @("STM32_Programmer_CLI.exe", "STM32_Programmer_CLI") `
        -CandidateFiles @("C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe") `
        -SearchCubeIde)

    $programmerExe = Join-Path $programmerBin "STM32_Programmer_CLI.exe"
    $externalLoader = Join-Path $programmerBin "ExternalLoader\MX25UM51245G_STM32N6570-NUCLEO.stldr"

    if (-not (Test-Path -LiteralPath $externalLoader -PathType Leaf)) {
        throw "External loader not found: $externalLoader"
    }

    Write-Step "Flashing model weights to external NOR"
    Invoke-External -FilePath $programmerExe -Arguments @(
        "-c", "port=SWD", ("mode={0}" -f $ConnectMode),
        "-el", $externalLoader,
        "-hardRst",
        "-w", $resolvedNetworkHex
    )

    Write-Step "Loading aiValidation firmware into SRAM"
    Invoke-External -FilePath $programmerExe -Arguments @(
        "-c", "port=SWD", ("mode={0}" -f $ConnectMode),
        "-w", $paths.ProjectHex,
        "-s", "0x34000000"
    )

    Write-Step "aiValidation firmware is running"
    Write-Host "Run: .\scripts\test-ai-runner-board.ps1 -Action summary -Desc $Desc"
}

function Invoke-Validate {
    Write-Step "Running AiRunner summary"
    & (Join-Path $RepoRoot "scripts\test-ai-runner-board.ps1") -Action summary -Desc $Desc
    if ($LASTEXITCODE -ne 0) {
        throw "AiRunner summary failed."
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$EdgeRoot = Find-STEdgeAiRoot

Write-Step "ST NPU aiValidation setup"
Write-Host "STEdgeAI       : $EdgeRoot"
Write-Host "Workspace      : $ValidationRoot"
Write-Host "Build conf     : $BuildConf"
Write-Host "Model artifacts: $(Resolve-RepoPath $ModelArtifacts)"

switch ($Action) {
    "prepare" { Invoke-Prepare }
    "build" {
        Invoke-Prepare
        Invoke-Build
    }
    "flash" { Invoke-Flash }
    "validate" { Invoke-Validate }
    "all" {
        Invoke-Prepare
        Invoke-Build
        Write-Step "Ready to deploy"
        Write-Host "Flash validation firmware: .\scripts\stm32n6-ai-validation.ps1 -Action flash"
        Write-Host "Validate after flashing  : .\scripts\test-ai-runner-board.ps1 -Action summary -Desc $Desc"
    }
}
