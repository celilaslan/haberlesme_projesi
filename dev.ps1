# Windows developer helper for build/run/demo/up/down/status/logs
# Usage examples (run in PowerShell):
#   .\dev.ps1 configure
#   .\dev.ps1 build
#   .\dev.ps1 run telemetry_service
#   .\dev.ps1 up UAV_1
#   .\dev.ps1 logs -f
#   .\dev.ps1 down

param(
    [Parameter(Position=0)] [string] $Command,
    [Parameter(ValueFromRemainingArguments=$true)] [string[]] $Args
)

$ErrorActionPreference = "Stop"

# --- Helpers ---
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $RepoRoot

function Have($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Get-ExePath([string]$relativeNoExt) {
    $p1 = Join-Path $RepoRoot ($relativeNoExt + ".exe")
    $p2 = Join-Path $RepoRoot $relativeNoExt
    if (Test-Path $p1) { return $p1 }
    elseif (Test-Path $p2) { return $p2 }
    else { return $p1 } # default to .exe path for Windows builds
}

function Kill-Procs {
    param([string[]] $names = @('telemetry_service','uav_sim','camera_ui','mapping_ui'))
    foreach ($n in $names) {
        try { Get-Process -Name $n -ErrorAction Stop | Stop-Process -Force } catch { }
    }
}

function List-Procs {
    param([string[]] $names = @('telemetry_service','uav_sim','camera_ui','mapping_ui'))
    Get-Process -ErrorAction SilentlyContinue | Where-Object { $names -contains $_.Name } |
        Select-Object Name, Id, CPU, StartTime | Format-Table -AutoSize
}

function List-Ports {
    param([int[]] $ports = @(5555,5557,5558,5559,5565,5569,5575,5579))
    try {
        $conns = Get-NetTCPConnection -State Listen -ErrorAction Stop | Where-Object { $ports -contains $_.LocalPort }
        if ($conns) { $conns | Select-Object LocalAddress,LocalPort,OwningProcess,State | Format-Table -AutoSize }
    } catch {
        Write-Verbose "Get-NetTCPConnection unavailable; skipping port list."
    }
}

function Wait-ForPort {
    param([int] $Port, [int] $TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $conn = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction Stop
            if ($conn) { return $true }
        } catch { }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Open-Term {
    param([string] $Title, [string] $CommandLine)
    # Prefer Windows Terminal if present
    if (Have 'wt') {
        Start-Process wt -ArgumentList @('new-tab','--title', $Title,'powershell','-NoExit','-NoProfile','-Command', $CommandLine) | Out-Null
    } else {
        Start-Process powershell -ArgumentList @('-NoExit','-NoProfile','-Command', $CommandLine) -WindowStyle Normal | Out-Null
    }
}

function Ensure-BuildDirs { if (!(Test-Path (Join-Path $RepoRoot 'build'))) { New-Item -ItemType Directory -Path (Join-Path $RepoRoot 'build') | Out-Null } }

function Configure {
    param([string] $Generator)
    Ensure-BuildDirs

    if (-not $Generator) {
        if (Have 'ninja') { $Generator = 'Ninja' }
        elseif (Have 'msbuild') { $Generator = 'Visual Studio 17 2022' }
        elseif (Have 'cmake') { $Generator = 'NMake Makefiles' }
        else { $Generator = 'Ninja' }
    }

    Write-Host "Configuring with generator: $Generator" -ForegroundColor Cyan
    & cmake -S $RepoRoot -B (Join-Path $RepoRoot 'build') -G "$Generator"
}

function Build {
    param([string[]] $Targets)
    Ensure-BuildDirs
    $args = @('--build', (Join-Path $RepoRoot 'build'))
    if ($Targets -and $Targets.Count -gt 0) { $args += @('--target'); $args += $Targets }
    # Prefer Release config where applicable
    $args += @('--config','Release')
    & cmake @args
}

function Clean {
    Write-Host "Stopping processes..." -ForegroundColor Yellow
    Kill-Procs
    Write-Host "Removing build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $RepoRoot 'build')
    # Remove in-source executables if present
    foreach ($p in @('telemetry_service/telemetry_service','uav_sim/uav_sim','camera_ui/camera_ui','mapping_ui/mapping_ui')) {
        $exe = Get-ExePath $p
        if (Test-Path $exe) { Remove-Item -Force $exe -ErrorAction SilentlyContinue }
    }
}

function Run-One {
    param([string] $name)
    $env:SERVICE_CONFIG = Join-Path $RepoRoot 'service_config.json'
    switch ($name) {
        'telemetry_service' { & (Get-ExePath 'telemetry_service/telemetry_service') }
        'uav_sim'           { & (Get-ExePath 'uav_sim/uav_sim') }
        'camera_ui'         { & (Get-ExePath 'camera_ui/camera_ui') }
        'mapping_ui'        { & (Get-ExePath 'mapping_ui/mapping_ui') }
        default { throw "Unknown component: $name" }
    }
}

function Up {
    param([string[]] $Uavs)
    $env:SERVICE_CONFIG = Join-Path $RepoRoot 'service_config.json'

    Write-Host "Ensuring prior processes are stopped..." -ForegroundColor Yellow
    Kill-Procs

    Write-Host "Starting telemetry_service..." -ForegroundColor Green
    $svcCmd = "Set-Location '$RepoRoot'; \$env:SERVICE_CONFIG='${env:SERVICE_CONFIG}'; & '$(Get-ExePath 'telemetry_service/telemetry_service')'"
    Open-Term -Title 'telemetry_service' -CommandLine $svcCmd
    if (-not (Wait-ForPort -Port 5557 -TimeoutSec 10)) { Write-Warning "Service publish port 5557 not listening yet." }

    Write-Host "Starting UIs..." -ForegroundColor Green
    $camCmd = "Set-Location '$RepoRoot'; \$env:SERVICE_CONFIG='${env:SERVICE_CONFIG}'; & '$(Get-ExePath 'camera_ui/camera_ui')'"
    $mapCmd = "Set-Location '$RepoRoot'; \$env:SERVICE_CONFIG='${env:SERVICE_CONFIG}'; & '$(Get-ExePath 'mapping_ui/mapping_ui')'"
    Open-Term -Title 'camera_ui' -CommandLine $camCmd
    Open-Term -Title 'mapping_ui' -CommandLine $mapCmd

    if (-not $Uavs -or $Uavs.Count -eq 0) { $Uavs = @('UAV_1') }
    foreach ($u in $Uavs) {
        Write-Host "Starting uav_sim $u..." -ForegroundColor Green
        $simCmd = "Set-Location '$RepoRoot'; \$env:SERVICE_CONFIG='${env:SERVICE_CONFIG}'; & '$(Get-ExePath 'uav_sim/uav_sim')' $u"
        Open-Term -Title "uav_sim $u" -CommandLine $simCmd
    }
}

function Down {
    Write-Host "Stopping processes..." -ForegroundColor Yellow
    List-Procs | Out-Host
    Kill-Procs
    Start-Sleep -Milliseconds 300
    Write-Host "After stop, process state:" -ForegroundColor Yellow
    List-Procs | Out-Host
    Write-Host "Ports in use:" -ForegroundColor Yellow
    List-Ports | Out-Host
}

function Status {
    Write-Host "Processes:" -ForegroundColor Cyan
    List-Procs | Out-Host
    Write-Host "Listening ports (common):" -ForegroundColor Cyan
    List-Ports | Out-Host
}

function Logs {
    param([switch] $Follow)
    $log = Join-Path $RepoRoot 'telemetry_service/telemetry_log.txt'
    if (-not (Test-Path $log)) { Write-Host "Log file not found yet: $log" -ForegroundColor Yellow }
    if ($Follow) { Get-Content -Path $log -Tail 200 -Wait }
    else { if (Test-Path $log) { Get-Content -Path $log -Tail 200 } }
}

function Demo {
    Write-Host "Demo: build, start service+UIs+UAV_1, then tail logs. Press Ctrl+C to stop tail; run '.\\dev.ps1 down' to stop processes." -ForegroundColor Cyan
    Kill-Procs
    Configure
    Build
    Up @('UAV_1')
    Start-Sleep -Seconds 1
    Logs -Follow
}

# --- Command Router ---
if (-not $Command) {
    Write-Host "Available commands: configure | build [targets...] | build-targets <t1> [t2..] | clean | rebuild | run <component> | demo | up [UAVs...] | down | status | logs [-f]" -ForegroundColor Cyan
    exit 0
}

switch ($Command) {
    'configure'      { Configure -Generator ($Args[0]) }
    'build'          { if ($Args) { Build -Targets $Args } else { Build } }
    'build-targets'  { if (-not $Args) { throw 'Specify one or more CMake targets' } else { Build -Targets $Args } }
    'clean'          { Clean }
    'rebuild'        { Clean; Configure; Build }
    'run'            { if (-not $Args) { throw 'Specify component: telemetry_service|uav_sim|camera_ui|mapping_ui' } else { Run-One -name $Args[0] } }
    'demo'           { Demo }
    'up'             { Up -Uavs $Args }
    'down'           { Down }
    'status'         { Status }
    'logs'           { $follow = ($Args -contains '-f' -or $Args -contains '--follow'); Logs -Follow:$follow }
    default          { throw "Unknown command: $Command" }
}
