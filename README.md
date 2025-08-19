# haberlesme_projesi

Cross-platform telemetry service and test apps using TCP (ZeroMQ) for commands and telemetry, and UDP (Boost.Asio) for additional telemetry streaming.

## Architecture

This is a **production-ready telemetry system** with proper protocol separation:

- **TCP (ZeroMQ)**: Bidirectional communication for commands, responses, and telemetry data
- **UDP (Boost.Asio)**: Additional telemetry streaming (lightweight, real-time)
- **Thread Safety**: Atomic operations, mutex protection, signal handlers
- **Responsive Shutdown**: All components respond immediately to Ctrl+C
- **Protocol Validation**: Strict argument validation and usage enforcement

**Key Features:**
- Production-grade error handling and resource management
- Non-blocking network operations preventing shutdown issues
- Comprehensive logging and monitoring capabilities
- Cross-platform development scripts with safety validations
- Industry-standard UAV communication protocols

## Build (Linux)

Fast path using helper script:

```bash
# From repo root
chmod +x ./dev.sh
./dev.sh configure
./dev.sh build
```

Manual build with g++:

```bash
# Build telemetry service (requires multiple source files)
g++ -std=c++17 telemetry_service/main.cpp telemetry_service/TelemetryService.cpp telemetry_service/Config.cpp telemetry_service/Logger.cpp telemetry_service/ZmqManager.cpp telemetry_service/UdpManager.cpp -lzmq -lboost_system -lpthread -o telemetry_service/telemetry_service

# Build other components (single file each - all require Boost.Asio for UDP)
g++ -std=c++17 uav_sim/uav_sim.cpp -lzmq -lboost_system -lpthread -o uav_sim/uav_sim
g++ -std=c++17 camera_ui/camera_ui.cpp -lzmq -lboost_system -lpthread -o camera_ui/camera_ui
g++ -std=c++17 mapping_ui/mapping_ui.cpp -lzmq -lboost_system -lpthread -o mapping_ui/mapping_ui
```

**Note**: All components require `-lboost_system` for Boost.Asio UDP networking support.

## Build (Windows)

Using PowerShell helper (recommended):

```powershell
# From repo root in PowerShell
.\dev.ps1 configure        # picks Ninja or VS generator
.\dev.ps1 build            # builds Release by default
```

Manual with CMake GUI or CLI:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### Advanced Build Options

The build system supports professional-grade configuration options:

```bash
# Configure with enhanced options
./dev.sh configure build Debug --warnings --debug --werror

# Install to custom location  
./dev.sh install /opt/telemetry-service

# Create distribution packages
./dev.sh package
```

**Available Build Options:**
- `--warnings`: Enable comprehensive compiler warnings
- `--debug`: Include debug information in binaries
- `--werror`: Treat warnings as errors for stricter builds

**CMake Variables:**
- `-DENABLE_WARNINGS=ON/OFF`: Control warning display
- `-DBUILD_WITH_DEBUG_INFO=ON/OFF`: Debug information inclusion
- `-DTREAT_WARNINGS_AS_ERRORS=ON/OFF`: Warning strictness
- `-DCMAKE_INSTALL_PREFIX=/path`: Installation directory

**PowerShell (Windows):**
```powershell
# Enhanced configuration with switches
.\dev.ps1 configure -EnableWarnings -DebugInfo -WarningsAsErrors

# Install and package
.\dev.ps1 install "C:\TelemetryService"
.\dev.ps1 package
```

## Run

**Important**: All UI applications require explicit protocol selection. UAVs receive commands only via TCP for security.

**Protocol Architecture:**
- **TCP (ZeroMQ)**: Commands, responses, and telemetry data (secure, reliable)
- **UDP (Boost.Asio)**: Additional telemetry streaming (lightweight, real-time)
- **Security**: UAVs do not accept commands via UDP (industry standard)

The `uav_sim` supports protocol selection:
- `--protocol tcp` - TCP only (ZeroMQ) - receives commands and sends telemetry via TCP
- `--protocol udp` - UDP only - sends telemetry data via UDP only (no command reception)
- Default (no --protocol) - Sends telemetry via both TCP and UDP simultaneously

```bash
# 1) Start the telemetry_service (production-ready with signal handling)
./dev.sh run telemetry_service

# 2) Start UI clients (protocol selection REQUIRED - enhanced validation)
./dev.sh run camera_ui --protocol tcp      # For sending commands and receiving telemetry via TCP
./dev.sh run mapping_ui --protocol udp     # For receiving telemetry via UDP only

# 3) Start UAV sims (enhanced argument validation)
./dev.sh run uav_sim UAV_1 --protocol tcp    # TCP: receives commands + sends telemetry via TCP
./dev.sh run uav_sim UAV_2 --protocol udp    # UDP: sends telemetry via UDP only
./dev.sh run uav_sim UAV_3                   # Both protocols: sends telemetry via TCP+UDP

# Enhanced helper commands with improved safety
./dev.sh demo           # Comprehensive system test with protocol validation
./dev.sh up UAV_1 UAV_2 # Launch everything (enhanced process management)
./dev.sh health         # Full system health check
./dev.sh status         # Show processes and ports with validation
```

On Windows (PowerShell - enhanced with matching safety features):

```powershell
# 1) Start service (production-ready)
.\dev.ps1 run telemetry_service

# 2) Start UIs with required protocol selection (enhanced validation)
.\dev.ps1 run camera_ui --protocol tcp     # Commands and telemetry via TCP
.\dev.ps1 run mapping_ui --protocol udp    # Telemetry via UDP

# 3) Start UAVs with protocol selection
.\dev.ps1 run uav_sim UAV_1 --protocol tcp  # TCP: commands + telemetry via TCP
.\dev.ps1 run uav_sim UAV_2 --protocol udp  # UDP: telemetry via UDP only
.\dev.ps1 run uav_sim UAV_3                 # Both protocols: telemetry via TCP+UDP

# Enhanced helper commands (improved process management)
.\dev.ps1 demo           # Comprehensive system test
.\dev.ps1 health         # Full system health check
.\dev.ps1 up UAV_1       # Launch with background process tracking
```

## Config

`service_config.json` defines the UAV endpoints, UI ports, and per-UAV UDP telemetry ports. Each UAV now has its own dedicated UDP port for better network isolation and security:

```json
{
  "uavs": [
    {
      "name": "UAV_1",
      "ip": "localhost",
      "telemetry_port": 5555,
      "command_port": 5559,
      "udp_telemetry_port": 5556
    }
  ]
}
```

The service writes logs to `log_file`. If relative, logs resolve next to the telemetry_service executable.

**Network Architecture**: 
- **TCP (ZeroMQ)**: Secure channel for commands and telemetry with PUB/SUB pattern for reliable message delivery
- **UDP (Boost.Asio)**: Additional lightweight telemetry streaming with per-UAV dedicated ports for isolation
- **Security**: UAVs receive commands only via TCP (industry standard), but send telemetry via both protocols
- **Thread Safety**: All network operations use atomic flags and non-blocking operations
- **Fault Tolerance**: Each UAV gets its own UDP server for better fault isolation

**Production Features:**
- Signal handlers for responsive shutdown (Ctrl+C works immediately)
- Non-blocking network operations prevent hanging
- Comprehensive error handling and resource cleanup
- Thread-safe operations with mutex protection
- Protocol validation throughout the system

Note: UI applications require explicit `--protocol` selection for security and clarity. UAV simulators support multiple modes but only receive commands via TCP. UAVs send telemetry via both TCP and UDP protocols depending on configuration. With TCP PUB/SUB (ZeroMQ), subscribers (UIs) may miss messages sent before they connect and set subscriptions. Starting UIs before UAV sims avoids losing early telemetry. UDP telemetry is connectionless and real-time.

## Notes
- **Production-Ready**: The project features comprehensive error handling, signal management, and thread safety
- **Protocol Security**: UAVs receive commands only via TCP (secure), but send telemetry via both TCP and UDP
- **Dependencies**: Boost.Asio for UDP networking, ZeroMQ for TCP communication, Boost.System for networking support
- **Cross-Platform**: Tested on Linux, macOS, and Windows with consistent behavior
- **Thread Safety**: All components use atomic operations and non-blocking network calls
- **Responsive Shutdown**: Signal handlers ensure immediate response to Ctrl+C across all components
- **Windows Compatibility**: Uses `localtime_s` on Windows and `localtime_r` on POSIX for thread safety
- **Network Architecture**: Dedicated UDP servers per UAV with individual IP:port binding for fault isolation

## Orchestration helpers
- **Linux/macOS**: `./dev.sh` - Production-ready script with enhanced process management, argument validation, and safety features
- **Windows**: `./dev.ps1` - PowerShell script with matching functionality and background process tracking
- **Enhanced Features**: Both scripts include protocol validation, safe process management, and comprehensive health checks
- **Safety**: Improved process killing using port-based identification instead of aggressive name matching
- **Validation**: Comprehensive argument validation with helpful error messages and usage examples

## Common workflows

Linux/macOS (enhanced safety and validation):

```bash
./dev.sh configure
./dev.sh build
./dev.sh health         # Comprehensive system health check
./dev.sh demo           # Production-ready smoke test with protocol validation
./dev.sh up UAV_1       # Open service, UIs, and UAV_1 with enhanced process tracking
./dev.sh status         # See PIDs and listening ports with validation
./dev.sh down           # Safe shutdown with improved process management
```

Windows (PowerShell - matching functionality):

```powershell
.\dev.ps1 configure
.\dev.ps1 build
.\dev.ps1 health        # Full system health check
.\dev.ps1 demo          # Enhanced demo with proper process tracking
.\dev.ps1 up UAV_1      # Launch with background process management
.\dev.ps1 status        # Process and port status with validation
.\dev.ps1 down          # Safe cleanup with background process tracking
```

## Dependencies

- **ZeroMQ**: TCP communication with PUB/SUB pattern
  - Linux: `sudo apt-get install -y libzmq3-dev`
  - Windows (vcpkg): `vcpkg install zeromq:x64-windows cppzmq:x64-windows`
- **Boost (including Boost.Asio)**: UDP networking and system utilities
  - Linux: `sudo apt-get install -y libboost-all-dev`
  - Windows (vcpkg): `vcpkg install boost:x64-windows`
  - **Key Components**: Boost.Asio for async UDP I/O, Boost.System for networking support

Configure with vcpkg toolchain (optional):
  - `cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake`

ZeroMQ version note:
- Tested with libzmq 4.3.x. Older 4.1+ typically works, but 4.3+ is recommended.

## Production-Ready Features

This telemetry system includes enterprise-grade reliability features:

### **Thread Safety & Concurrency**
- Atomic flags for coordinated shutdown across threads
- Mutex protection for shared resources
- Non-blocking network operations prevent hanging
- Thread-safe logging with proper synchronization

### **Signal Handling & Shutdown**
- Responsive Ctrl+C handling in all components
- Clean resource cleanup on shutdown
- No more hanging processes or orphaned resources
- Graceful connection termination

### **Protocol Security & Validation**
- UAVs receive commands only via TCP (industry standard)
- UAVs send telemetry via both TCP and UDP protocols for redundancy and flexibility
- UDP restricted from receiving commands (security best practice)
- Comprehensive argument validation with clear error messages
- Protocol compliance enforcement throughout

### **Development Tools**
- Enhanced build scripts with safety validations
- Background process tracking and cleanup
- Port-based process identification (no aggressive killing)
- Cross-platform consistency between bash and PowerShell scripts

### **Error Handling & Monitoring**
- Comprehensive exception handling throughout
- Production-quality logging with timestamps
- Health check capabilities for system monitoring
- Resource leak prevention and cleanup

## Troubleshooting

### **Common Issues (Enhanced Solutions)**

- **Address already in use**:
	- Linux/macOS:
		- `./dev.sh down` - Enhanced safe shutdown with process validation
		- `./dev.sh status` - Shows listeners with improved port checking
		- `./dev.sh health` - Comprehensive system diagnosis
		- Enhanced process management avoids orphaned processes
	- Windows:
		- `.\dev.ps1 down` - Improved cleanup with background process tracking
		- `.\dev.ps1 status` - Enhanced port and process monitoring
		- `.\dev.ps1 health` - Full system health check

- **Components not responding to Ctrl+C**:
	- **Fixed**: All components now have responsive signal handlers
	- **Solution**: Enhanced with atomic flags for immediate shutdown response
	- **Verification**: Use `./dev.sh health` to check responsive shutdown capability

- **Protocol validation errors**:
	- **Feature**: Enhanced argument validation with clear error messages
	- **Solution**: UI components require explicit `--protocol tcp|udp` parameter
	- **Example**: `./dev.sh run camera_ui --protocol tcp` (provides helpful usage info)

- **Script execution issues**:
	- **Enhanced**: Both dev.sh and dev.ps1 now include comprehensive validation
	- **Safety**: Process management uses port-based identification
	- **Validation**: Clear error messages for invalid arguments with examples

- **Windows script execution policy**:
	- If PowerShell blocks scripts, run: `Set-ExecutionPolicy -Scope Process Bypass`
	- **Enhanced**: PowerShell script now includes background process tracking

## Deployment (Linux Service)
The `telemetry_service` can be installed and run as a `systemd` background service on modern Linux distributions. It will listen for both ZMQ and per-UAV UDP telemetry on their respective ports.

1.  **Run the installer with sudo:**
    ```bash
    sudo ./install_linux_service.sh
    ```
    This script will build the project, copy the executable and configuration to system directories (`/usr/local/bin` and `/etc/telemetry_service`), and set up the `systemd` service.

2.  **Manage the service**:
    You can now manage the service using the convenient `dev.sh` commands, which wrap `systemctl` and `journalctl`. These commands require `sudo`.

    -   **Start the service**:
        ```bash
        ./dev.sh service-start
        ```
    -   **Check its status**:
        ```bash
        ./dev.sh service-status
        ```
    -   **View live logs**:
        ```bash
        ./dev.sh service-logs -f
        ```
    -   **Stop the service**:
        ```bash
        ./dev.sh service-stop
        ```

The service is automatically enabled to start on boot. The service's log file is located at `/var/log/telemetry_service/telemetry_log.txt`, and its configuration is managed at `/etc/telemetry_service/service_config.json`.
