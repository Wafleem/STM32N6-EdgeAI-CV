param(
    [ValidateSet("NUCLEO-N657X0-Q")]
    [string]$Board = "NUCLEO-N657X0-Q",

    [ValidateSet("UVCL", "SPI")]
    [string]$Interface = "UVCL",

    [ValidateSet("Generic", "SafalObb")]
    [string]$ModelProfile = "Generic",

    [int]$Jobs = 1,

    [switch]$AppOnly,

    [switch]$Clean
)

$action = if ($AppOnly) { "flash-app" } else { "flash-all" }

& (Join-Path $PSScriptRoot "scripts\stm32n6.ps1") `
    -Board $Board `
    -Action $action `
    -Interface $Interface `
    -ModelProfile $ModelProfile `
    -Jobs $Jobs `
    -Clean:$Clean
