param(
    [ValidateSet("NUCLEO-N657X0-Q", "STM32N6570-DK")]
    [string]$Board = "NUCLEO-N657X0-Q",

    [ValidateSet("UVCL", "SPI")]
    [string]$Interface = "UVCL",

    [int]$Jobs = 1,

    [switch]$AppOnly,

    [switch]$Clean
)

$action = if ($AppOnly) { "flash-app" } else { "flash-all" }

& (Join-Path $PSScriptRoot "scripts\stm32n6.ps1") `
    -Board $Board `
    -Action $action `
    -Interface $Interface `
    -Jobs $Jobs `
    -Clean:$Clean
