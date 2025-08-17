# Windows developer helper for telemetry communication project
# Usage:
#   .\dev.ps1 <command> [options]
#
# Build Commands:
#   configure [generator] [build_type] - Run CMake to configure the project (Debug/Release).
#   build [targets...]      - Build all targets or specific targets.
#   rebuild                 - Clean, configure, and build the project.
#   clean                   - Clean the build directory and executables.
#   watch                   - Watch for file changes and rebuild automatically.
#
# Runtime Commands:
#   run <target> [args...]  - Run a specific executable with arguments.
#     <target>: telemetry_service, uav_sim, camera_ui, mapping_ui
#
#   up [UAVs...] [args...]  - Launch service, UIs, and specified UAVs in new terminals.
#                             Any extra arguments are passed to all UAV simulators.
#                             Defaults to launching UAV_1 if no UAVs are specified.
#   down                    - Stop all running components.
#   status                  - Show running processes and listening ports.
#
# Testing and Validation:
#   demo                    - Run a quick, self-contained test of the system.
#   test                    - Run project tests or smoke test if no tests available.
#   health                  - Comprehensive system health check.
#   logs [-f]               - Show the tail of the service log file (-f to follow).
#
# System Information:
#   info                    - Show environment and project information.
#   deps                    - Check for required dependencies.
#   validate [config_file]  - Validate service configuration file.
#
# Defaults:
#   generator = Auto-detect (Ninja > Visual Studio > NMake)
#   build_type = Release
#
# Examples:
#   .\dev.ps1 configure "Visual Studio 17 2022" Debug
#   .\dev.ps1 build
#   .\dev.ps1 health                   # Check system health
#   .\dev.ps1 watch                    # Auto-rebuild on changes
#   .\dev.ps1 run telemetry_service
#   .\dev.ps1 run uav_sim UAV_1 --protocol udp
#   .\dev.ps1 up UAV_1 UAV_2 --protocol udp
#   .\dev.ps1 down
#   .\dev.ps1 logs -f

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

function Get-CmakePath {
    # 1. Check if it's in the PATH
    if (Have 'cmake') { return 'cmake' }

    # 2. Check common installation directories
    $commonPaths = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe",
        "$env:LOCALAPPDATA\Programs\CMake\bin\cmake.exe", # User-specific install
        "$env:ProgramData\chocolatey\bin\cmake.exe"
    )
    foreach ($p in $commonPaths) {
        if (Test-Path $p) {
            Write-Host "Found cmake at $p" -ForegroundColor Gray
            return $p
        }
    }
    return $null
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

function Ensure-BuildDirs { 
    if (!(Test-Path (Join-Path $RepoRoot 'build'))) { 
        New-Item -ItemType Directory -Path (Join-Path $RepoRoot 'build') | Out-Null 
    } 
}

# Validate service configuration file
function Test-Config {
    param([string] $ConfigFile = (Join-Path $RepoRoot 'service_config.json'))
    
    if (!(Test-Path $ConfigFile)) {
        Write-Error "Configuration file not found: $ConfigFile"
        return $false
    }
    
    try {
        $content = Get-Content $ConfigFile -Raw | ConvertFrom-Json
        Write-Host "Configuration file validated: $ConfigFile" -ForegroundColor Green
        return $true
    } catch {
        Write-Error "Invalid JSON in configuration file: $ConfigFile - $($_.Exception.Message)"
        return $false
    }
}

# Check for required dependencies
function Test-Dependencies {
    $missing = @()
    $optional_missing = @()
    
    # Required dependencies
    if (!(Have 'cmake')) { $missing += 'cmake' }
    if (!(Have 'cl') -and !(Have 'gcc') -and !(Have 'clang')) { 
        $missing += 'Visual Studio Build Tools, GCC, or Clang compiler' 
    }
    
    # Optional but recommended
    if (!(Have 'python')) { $optional_missing += 'python (for config validation)' }
    if (!(Have 'ninja')) { $optional_missing += 'ninja (faster builds)' }
    if (!(Have 'git')) { $optional_missing += 'git (version control)' }
    
    # Check for vcpkg (common on Windows)
    $vcpkgPath = Join-Path $RepoRoot 'vcpkg'
    if (!(Test-Path $vcpkgPath)) {
        $optional_missing += 'vcpkg (dependency management - install in project root)'
    }
    
    if ($missing.Count -gt 0) {
        Write-Host "Error: Missing required dependencies:" -ForegroundColor Red
        $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        return $false
    }
    
    if ($optional_missing.Count -gt 0) {
        Write-Host "Warning: Missing optional dependencies:" -ForegroundColor Yellow
        $optional_missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    }
    
    Write-Host "Dependencies check passed" -ForegroundColor Green
    return $true
}

# Comprehensive health check
function Invoke-HealthCheck {
    Write-Host "=== System Health Check ===" -ForegroundColor Cyan
    
    # Check dependencies
    Write-Host "Checking dependencies..." -ForegroundColor Gray
    if (Test-Dependencies) {
        Write-Host "✓ Dependencies OK" -ForegroundColor Green
    } else {
        Write-Host "✗ Dependencies issues found" -ForegroundColor Red
    }
    
    # Check configuration
    Write-Host "Checking configuration..." -ForegroundColor Gray
    if (Test-Config) {
        Write-Host "✓ Configuration OK" -ForegroundColor Green
    } else {
        Write-Host "✗ Configuration issues found" -ForegroundColor Red
    }
    
    # Check build status
    Write-Host "Checking build status..." -ForegroundColor Gray
    $missing_exes = @()
    $exes = @('telemetry_service/telemetry_service', 'uav_sim/uav_sim', 'camera_ui/camera_ui', 'mapping_ui/mapping_ui')
    foreach ($exe in $exes) {
        $path = Get-ExePath $exe
        if (!(Test-Path $path)) {
            $missing_exes += $exe
        }
    }
    
    if ($missing_exes.Count -eq 0) {
        Write-Host "✓ All executables built" -ForegroundColor Green
    } else {
        Write-Host "✗ Missing executables: $($missing_exes -join ', ')" -ForegroundColor Red
        Write-Host "  Run '.\dev.ps1 build' to build them" -ForegroundColor Yellow
    }
    
    # Check running processes
    Write-Host "Checking running processes..." -ForegroundColor Gray
    $procs = Get-Process -ErrorAction SilentlyContinue | Where-Object { 
        @('telemetry_service','uav_sim','camera_ui','mapping_ui') -contains $_.Name 
    }
    if ($procs) {
        Write-Host "✓ Project processes running:" -ForegroundColor Green
        $procs | Select-Object Name, Id, CPU | Format-Table -AutoSize
    } else {
        Write-Host "ℹ No project processes currently running" -ForegroundColor Blue
    }
    
    # Check listening ports
    Write-Host "Checking network ports..." -ForegroundColor Gray
    $ports = @(5555,5556,5557,5558,5559,5565,5569,5575,5579)
    try {
        $conns = Get-NetTCPConnection -State Listen -ErrorAction Stop | Where-Object { $ports -contains $_.LocalPort }
        if ($conns) {
            Write-Host "✓ Project ports in use:" -ForegroundColor Green
            $conns | Select-Object LocalAddress,LocalPort,OwningProcess,State | Format-Table -AutoSize
        } else {
            Write-Host "ℹ No project ports currently in use" -ForegroundColor Blue
        }
    } catch {
        Write-Host "ℹ Cannot check network ports (requires admin privileges)" -ForegroundColor Blue
    }
    
    Write-Host "=== Health Check Complete ===" -ForegroundColor Cyan
}

# Show environment and project information
function Show-Info {
    Write-Host "=== Project Information ===" -ForegroundColor Cyan
    Write-Host "Project Root: $RepoRoot"
    Write-Host "Build Directory: $(Join-Path $RepoRoot 'build')"
    
    try {
        $branch = git branch --show-current 2>$null
        if ($branch) { Write-Host "Current Branch: $branch" }
        else { Write-Host "Current Branch: Not a git repo" }
        
        $commit = git log -1 --oneline 2>$null
        if ($commit) { Write-Host "Last Commit: $commit" }
        else { Write-Host "Last Commit: N/A" }
    } catch {
        Write-Host "Git Status: Not available"
    }
    
    Write-Host ""
    Write-Host "=== System Information ===" -ForegroundColor Cyan
    Write-Host "OS: $($PSVersionTable.OS -replace "`n", " ")"
    Write-Host "PowerShell: $($PSVersionTable.PSVersion)"
    Write-Host "Architecture: $($env:PROCESSOR_ARCHITECTURE)"
    Write-Host "CPU Cores: $($env:NUMBER_OF_PROCESSORS)"
    
    try {
        $memory = Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue
        if ($memory) {
            $memGB = [Math]::Round($memory.TotalPhysicalMemory / 1GB, 1)
            Write-Host "Total RAM: ${memGB} GB"
        }
    } catch {
        Write-Host "Total RAM: Unknown"
    }
    
    Write-Host ""
    Write-Host "=== Build Tools ===" -ForegroundColor Cyan
    
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) { Write-Host "CMake: $(cmake --version | Select-Object -First 1)" }
    else { Write-Host "CMake: Not found" }
    
    $compiler = $null
    if (Get-Command cl -ErrorAction SilentlyContinue) { 
        $compiler = "MSVC (Visual Studio)"
    } elseif (Get-Command gcc -ErrorAction SilentlyContinue) {
        $compiler = "GCC $(gcc --version | Select-Object -First 1)"
    } elseif (Get-Command clang -ErrorAction SilentlyContinue) {
        $compiler = "Clang $(clang --version | Select-Object -First 1)"
    }
    Write-Host "Compiler: $($compiler ?? 'Not found')"
    
    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($ninja) { Write-Host "Ninja: $(ninja --version)" }
    else { Write-Host "Ninja: Not found" }
    
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) { Write-Host "Python: $(python --version)" }
    else { Write-Host "Python: Not found" }
    
    Write-Host ""
    Write-Host "=== Configuration ===" -ForegroundColor Cyan
    $configPath = Join-Path $RepoRoot 'service_config.json'
    if (Test-Path $configPath) {
        Write-Host "Service Config: Found"
        try {
            $config = Get-Content $configPath -Raw | ConvertFrom-Json
            Write-Host "Configured UAVs: $($config.uavs.Count)"
        } catch {
            Write-Host "Configured UAVs: Unknown (parse error)"
        }
    } else {
        Write-Host "Service Config: Not found"
    }
    
    # Check for vcpkg
    $vcpkgPath = Join-Path $RepoRoot 'vcpkg'
    if (Test-Path $vcpkgPath) {
        Write-Host "vcpkg: Found at $vcpkgPath"
    } else {
        Write-Host "vcpkg: Not found"
    }
}

# Watch for file changes and rebuild automatically
function Invoke-WatchMode {
    param([string] $Generator)
    
    Write-Host "Starting watch mode. Press Ctrl+C to stop." -ForegroundColor Green
    Write-Host "Watching for changes in: $RepoRoot" -ForegroundColor Gray
    
    # Create FileSystemWatcher
    $watcher = New-Object System.IO.FileSystemWatcher
    $watcher.Path = $RepoRoot
    $watcher.IncludeSubdirectories = $true
    $watcher.Filter = "*.cpp"
    $watcher.NotifyFilter = [System.IO.NotifyFilters]::LastWrite
    
    # Create additional watchers for other file types
    $watchers = @($watcher)
    foreach ($ext in @("*.h", "*.hpp", "*.c", "*.cc", "*.cxx")) {
        $w = New-Object System.IO.FileSystemWatcher
        $w.Path = $RepoRoot
        $w.IncludeSubdirectories = $true
        $w.Filter = $ext
        $w.NotifyFilter = [System.IO.NotifyFilters]::LastWrite
        $watchers += $w
    }
    
    # Define the action
    $action = {
        $path = $Event.SourceEventArgs.FullPath
        $name = $Event.SourceEventArgs.Name
        $changeType = $Event.SourceEventArgs.ChangeType
        
        Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] File changed: $name" -ForegroundColor Yellow
        
        # Debounce rapid changes
        Start-Sleep -Milliseconds 500
        
        Write-Host "Rebuilding..." -ForegroundColor Cyan
        try {
            if ($Generator) { Configure -Generator $Generator }
            else { Configure }
            Build
            Write-Host "Build successful at $((Get-Date).ToString('HH:mm:ss'))" -ForegroundColor Green
        } catch {
            Write-Host "Build failed at $((Get-Date).ToString('HH:mm:ss')): $($_.Exception.Message)" -ForegroundColor Red
        }
    }
    
    # Register event handlers
    $jobs = @()
    foreach ($w in $watchers) {
        $w.EnableRaisingEvents = $true
        $jobs += Register-ObjectEvent -InputObject $w -EventName "Changed" -Action $action
    }
    
    try {
        Write-Host "Watching for changes... (Press Ctrl+C to stop)" -ForegroundColor Green
        while ($true) {
            Start-Sleep -Seconds 1
        }
    } finally {
        # Cleanup
        foreach ($job in $jobs) {
            Unregister-Event -SourceIdentifier $job.Name
        }
        foreach ($w in $watchers) {
            $w.Dispose()
        }
        Write-Host "Watch mode stopped." -ForegroundColor Yellow
    }
}

function Configure {
    param([string] $Generator, [string] $BuildType = "Release")
    Ensure-BuildDirs

    if (-not $Generator) {
        if (Have 'ninja') { $Generator = 'Ninja' }
        elseif (Have 'msbuild') { $Generator = 'Visual Studio 17 2022' }
        elseif (Have 'cmake') { $Generator = 'NMake Makefiles' }
        else { $Generator = 'Ninja' }
    }

    # If NMake selected but nmake.exe not available, fallback to VS generator (more robust on Windows)
    if ($Generator -eq 'NMake Makefiles' -and -not (Have 'nmake')) {
        Write-Host 'nmake not found; switching generator to Visual Studio 17 2022 (x64)' -ForegroundColor Yellow
        $Generator = 'Visual Studio 17 2022'
    }

    Write-Host "Configuring with generator: $Generator (Build Type: $BuildType)" -ForegroundColor Cyan
    $cmakePath = Get-CmakePath
    if (-not $cmakePath) { throw "CMake not found. Please install it and ensure it's in your PATH." }
    $buildDir = Join-Path $RepoRoot 'build'
    $toolchainArg = @()
    # Build the vcpkg toolchain path safely (Join-Path only supports one child at a time)
    $vcpkgToolchain = [IO.Path]::Combine($RepoRoot,'vcpkg','scripts','buildsystems','vcpkg.cmake')
    if (Test-Path $vcpkgToolchain) {
        Write-Host "Detected vcpkg toolchain: $vcpkgToolchain" -ForegroundColor Gray
        $toolchainArg = @('-DCMAKE_TOOLCHAIN_FILE=' + $vcpkgToolchain)
        if (-not $Env:VCPKG_TARGET_TRIPLET) { $toolchainArg += '-DVCPKG_TARGET_TRIPLET=x64-windows' }
    }
    $extra = @('-DCMAKE_BUILD_TYPE=' + $BuildType)
    if ($Generator -like 'Visual Studio*' -and -not $env:CMAKE_GENERATOR_PLATFORM) {
        # Ensure 64-bit build
        $extra += '-A'; $extra += 'x64'
    }
    & $cmakePath -S $RepoRoot -B $buildDir -G "$Generator" @toolchainArg @extra
}

function Build {
    param([string[]] $Targets)
    Ensure-BuildDirs
    $cmakePath = Get-CmakePath
    if (-not $cmakePath) { throw "CMake not found." }
    $args = @('--build', (Join-Path $RepoRoot 'build'))
    if ($Targets -and $Targets.Count -gt 0) { $args += @('--target'); $args += $Targets }
    # Prefer Release config where applicable
    $args += @('--config','Release')
    & $cmakePath @args
}

# Run tests if available
function Invoke-Tests {
    $buildDir = Join-Path $RepoRoot 'build'
    if (!(Test-Path $buildDir)) {
        Write-Error "Build directory not found. Run '.\dev.ps1 configure' first."
        return
    }
    
    Write-Host "Running tests..." -ForegroundColor Cyan
    $ctestFile = Join-Path $buildDir 'CTestTestfile.cmake'
    if (Test-Path $ctestFile) {
        Push-Location $buildDir
        try {
            & ctest --output-on-failure
        } finally {
            Pop-Location
        }
    } else {
        Write-Host "No tests configured in CMake. Running smoke test instead..." -ForegroundColor Yellow
        Invoke-DemoTest
    }
}

# Run a comprehensive demo/smoke test
function Invoke-DemoTest {
    Write-Host "Starting comprehensive demo test..." -ForegroundColor Cyan
    
    # Ensure executables exist
    $exes = @('telemetry_service/telemetry_service', 'uav_sim/uav_sim', 'camera_ui/camera_ui', 'mapping_ui/mapping_ui')
    foreach ($exe in $exes) {
        $path = Get-ExePath $exe
        if (!(Test-Path $path)) {
            Write-Error "Missing $exe. Run '.\dev.ps1 build' first."
            return
        }
    }
    
    # Stop any existing processes
    Kill-Procs
    
    Write-Host "Starting telemetry service..." -ForegroundColor Green
    $env:SERVICE_CONFIG = Join-Path $RepoRoot 'service_config.json'
    $serviceProcess = Start-Process -FilePath (Get-ExePath 'telemetry_service/telemetry_service') -PassThru -NoNewWindow
    
    Start-Sleep -Seconds 2
    
    if ($serviceProcess.HasExited) {
        Write-Error "Service failed to start"
        return
    }
    
    Write-Host "Starting UIs briefly..." -ForegroundColor Green
    $cameraProcess = Start-Process -FilePath (Get-ExePath 'camera_ui/camera_ui') -PassThru -NoNewWindow
    $mappingProcess = Start-Process -FilePath (Get-ExePath 'mapping_ui/mapping_ui') -PassThru -NoNewWindow
    
    Start-Sleep -Milliseconds 500
    
    Write-Host "Starting UAV simulator briefly..." -ForegroundColor Green
    $uavProcess = Start-Process -FilePath (Get-ExePath 'uav_sim/uav_sim') -ArgumentList 'UAV_1' -PassThru -NoNewWindow
    
    Start-Sleep -Seconds 3
    
    # Stop all processes
    Write-Host "Stopping test processes..." -ForegroundColor Yellow
    @($uavProcess, $cameraProcess, $mappingProcess) | Where-Object { $_ -and !$_.HasExited } | ForEach-Object {
        $_.CloseMainWindow()
        if (!$_.WaitForExit(2000)) { $_.Kill() }
    }
    
    if (!$serviceProcess.HasExited) {
        $serviceProcess.CloseMainWindow()
        if (!$serviceProcess.WaitForExit(2000)) { $serviceProcess.Kill() }
    }
    
    # Show log tail
    $logPath = Join-Path $RepoRoot 'telemetry_service/telemetry_log.txt'
    if (Test-Path $logPath) {
        Write-Host "--- LOG TAIL ---" -ForegroundColor Cyan
        Get-Content -Path $logPath -Tail 12
    } else {
        Write-Host "LOG_NOT_FOUND" -ForegroundColor Yellow
    }
    
    Write-Host "Demo test completed." -ForegroundColor Green
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
    Write-Host "Demo: build, configure, and run comprehensive test" -ForegroundColor Cyan
    Kill-Procs
    Configure
    Build
    Invoke-DemoTest
}

# --- Command Router ---
if (-not $Command) {
    Write-Host @"
Available commands:
  Build Commands:    configure | build | rebuild | clean | watch
  Runtime Commands:  run | up | down | status | logs
  Testing:           demo | test | health
  Information:       info | deps | validate
  
Use '.\dev.ps1 <command> -?' for detailed help on each command.
"@ -ForegroundColor Cyan
    exit 0
}

switch ($Command) {
    'configure'      { 
        $generator = if ($Args.Count -gt 0) { $Args[0] } else { $null }
        $buildType = if ($Args.Count -gt 1) { $Args[1] } else { "Release" }
        Configure -Generator $generator -BuildType $buildType 
    }
    'build'          { if ($Args) { Build -Targets $Args } else { Build } }
    'build-targets'  { if (-not $Args) { throw 'Specify one or more CMake targets' } else { Build -Targets $Args } }
    'clean'          { Clean }
    'rebuild'        { Clean; Configure; Build }
    'watch'          { 
        $generator = if ($Args.Count -gt 0) { $Args[0] } else { $null }
        Invoke-WatchMode -Generator $generator 
    }
    'run'            { 
        if (-not $Args) { throw 'Specify component: telemetry_service|uav_sim|camera_ui|mapping_ui' } 
        else { Run-One -name $Args[0] } 
    }
    'demo'           { Demo }
    'test'           { Invoke-Tests }
    'health'         { Invoke-HealthCheck }
    'info'           { Show-Info }
    'deps'           { Test-Dependencies | Out-Null }
    'validate'       { 
        $configFile = if ($Args.Count -gt 0) { $Args[0] } else { $null }
        Test-Config -ConfigFile $configFile | Out-Null 
    }
    'up'             { Up -Uavs $Args }
    'down'           { Down }
    'status'         { Status }
    'logs'           { $follow = ($Args -contains '-f' -or $Args -contains '--follow'); Logs -Follow:$follow }
    default          { throw "Unknown command: $Command. Run '.\dev.ps1' for available commands." }
}
