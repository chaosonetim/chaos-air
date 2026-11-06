param(
    [Parameter(Mandatory = $true)]
    [string]$ProbeSerial,
    [string]$Port = 'COM3',
    [switch]$FullFrame,
    [switch]$AutoIsp,
    [string]$ComparisonPrefix
)

$ErrorActionPreference = 'Stop'
if ($AutoIsp -and -not $FullFrame) { throw '-AutoIsp requires -FullFrame' }
if ($ComparisonPrefix -and -not $AutoIsp) { throw '-ComparisonPrefix requires -AutoIsp' }
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    & cmake -S .\AppliSecure -B .\build\snapshot-secure -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$root\gcc-arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Debug -DCAMERA_SNAPSHOT=ON
    if ($LASTEXITCODE -ne 0) { throw 'Secure configure failed' }
    & cmake --build .\build\snapshot-secure
    if ($LASTEXITCODE -ne 0) { throw 'Secure build failed' }
    $fullFrameOption = if ($FullFrame) { 'ON' } else { 'OFF' }
    $autoIspOption = if ($AutoIsp) { 'ON' } else { 'OFF' }
    $compareOption = if ($ComparisonPrefix) { 'ON' } else { 'OFF' }
    & cmake -S .\AppliNonSecure -B .\build\snapshot-ns -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$root\gcc-arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Debug -DCAMERA_SNAPSHOT=ON "-DCAMERA_FULL_FRAME=$fullFrameOption" "-DCAMERA_AUTO_ISP=$autoIspOption" "-DCAMERA_COMPARE_SHARPENING=$compareOption" "-DCAMERA_SECURE_IMPORT=$root\build\snapshot-secure\secure_nsclib.o"
    if ($LASTEXITCODE -ne 0) { throw 'AppNS configure failed' }
    & cmake --build .\build\snapshot-ns
    if ($LASTEXITCODE -ne 0) { throw 'AppNS build failed' }

    $connection = @('-c', 'port=SWD', "sn=$ProbeSerial", 'mode=HOTPLUG', 'freq=1000')
    # Reset first so a preceding non-secure run cannot affect the secure entry.
    & STM32_Programmer_CLI -c port=SWD "sn=$ProbeSerial" mode=UR reset=HWrst freq=1000 -halt
    if ($LASTEXITCODE -ne 0) { throw 'Under-reset connection failed' }
    $secure = '.\build\snapshot-secure\chaos_air_AppliSecure.elf'
    $nonsecure = '.\build\snapshot-ns\chaos_air_AppliNonSecure.elf'
    & STM32_Programmer_CLI @connection -halt -w $secure -v -w $nonsecure -v
    if ($LASTEXITCODE -ne 0) { throw 'SRAM download/verification failed; check development boot mode' }
    $symbols = & arm-none-eabi-nm -n $secure
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read secure ELF symbols' }
    $reset = ($symbols | Select-String ' W Reset_Handler$').ToString().Split()[0]
    $stack = ($symbols | Select-String ' _estack$').ToString().Split()[0]
    $serial = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
    try {
        $serial.Open()
        & STM32_Programmer_CLI @connection -halt -coreReg "MSP=0x$stack" "PC=0x$reset" XPSR=0x01000000 PRIMASK=0 -run
        if ($LASTEXITCODE -ne 0) { throw 'Starting secure boot failed' }
        $log = ''
        if ($ComparisonPrefix) {
            $deadline = (Get-Date).AddSeconds(20)
            while ($log -notmatch 'COMPARE: unsharpened frame ready' -and (Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 100
                $log += $serial.ReadExisting()
            }
            if ($log -notmatch 'COMPARE: unsharpened frame ready') {
                Write-Output $log
                throw 'No unsharpened comparison frame ready'
            }
            & STM32_Programmer_CLI @connection -u 0x24120020 614400 "$ComparisonPrefix-before.bin"
            if ($LASTEXITCODE -ne 0) { throw 'Unsharpened comparison export failed' }
            $serial.Write('S')
        }
        Start-Sleep -Seconds $(if ($AutoIsp) { 15 } else { 8 })
        $log += $serial.ReadExisting()
        Write-Output $log
        if ($log -notmatch 'DONE: one frame retained') {
            throw 'AppNS did not confirm one complete frame; inspect UART log and capture state'
        }
        if ($ComparisonPrefix) {
            & STM32_Programmer_CLI @connection -u 0x24120020 614400 "$ComparisonPrefix-after.bin"
            if ($LASTEXITCODE -ne 0) { throw 'Sharpened comparison export failed' }
        }
    } finally {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
} finally {
    Pop-Location
}
