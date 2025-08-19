# haberlesme_projesi

Cross-platform telemetry service and test apps using TCP (ZeroMQ) and UDP protocols.

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

# Build other components (single file each)
g++ -std=c++17 uav_sim/uav_sim.cpp -lzmq -lboost_system -lpthread -o uav_sim/uav_sim
g++ -std=c++17 camera_ui/camera_ui.cpp -lzmq -lpthread -o camera_ui/camera_ui
g++ -std=c++17 mapping_ui/mapping_ui.cpp -lzmq -lpthread -o mapping_ui/mapping_ui
```

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

## Run

**Important**: All UI applications now require explicit protocol selection for professional usage.

The `uav_sim` supports three protocol modes:
- `--protocol tcp` - TCP only (ZeroMQ)
- `--protocol udp` - UDP only  
- Default (no --protocol) - Both TCP and UDP simultaneously

```bash
# 1) Start the telemetry_service
# It listens on both TCP and UDP ports simultaneously.
# The service can be run directly or as a systemd service (see Deployment section).
./dev.sh run telemetry_service

# 2) Start the UI clients (protocol selection is REQUIRED)
./dev.sh run camera_ui --protocol tcp
./dev.sh run mapping_ui --protocol udp

# 3) Start UAV sims with protocol selection
./dev.sh run uav_sim UAV_1 --protocol tcp    # TCP only
./dev.sh run uav_sim UAV_2 --protocol udp    # UDP only  
./dev.sh run uav_sim UAV_3                   # Both protocols (default)

# Alternative: Use helper commands for comprehensive testing
./dev.sh demo           # Quick system test with mixed protocols
./dev.sh protocol-test  # Comprehensive test of all protocol combinations
./dev.sh up UAV_1 UAV_2 --protocol udp  # Launch everything in terminals
```

On Windows (PowerShell):

```powershell
# 1) Start service
.\dev.ps1 run telemetry_service

# 2) Start UIs with required protocol selection
.\dev.ps1 run camera_ui --protocol tcp
.\dev.ps1 run mapping_ui --protocol udp

# 3) Start UAVs with protocol selection
.\dev.ps1 run uav_sim UAV_1 --protocol tcp  # TCP only
.\dev.ps1 run uav_sim UAV_2 --protocol udp  # UDP only
.\dev.ps1 run uav_sim UAV_3                 # Both protocols (default)

# Use helper commands for testing
.\dev.ps1 demo           # Quick system test
.\dev.ps1 up UAV_1       # Launch everything in terminals
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

**Network Architecture**: Each UAV gets its own UDP server bound to its specific IP and port combination, providing:
- Better fault isolation between UAVs
- Individual network security per UAV  
- Support for UAVs on different network interfaces
- Easier troubleshooting and monitoring

Note: UI applications require explicit `--protocol` selection (tcp or udp) for professional usage. UAV simulators support three modes: tcp-only, udp-only, or both protocols simultaneously (default). With TCP PUB/SUB (ZeroMQ), subscribers (UIs) may miss messages sent before they connect and set subscriptions. Starting UIs before UAV sims avoids losing early telemetry. This does not apply to UDP, as it is a connectionless protocol.

## Notes
- The project now depends on the Boost C++ libraries for UDP networking.
- For Windows builds, link against libzmq and Boost, and define the same C++17 flags; the code uses `localtime_s` on Windows and `localtime_r` on POSIX.
- The service creates dedicated UDP servers for each UAV configuration, with each server binding to the UAV's specific IP and UDP port.
- The service uses a poll-based receiver loop for ZMQ and asynchronous listeners for per-UAV UDP servers to avoid busy waiting.

## Orchestration helpers
- Linux/macOS: `./dev.sh` supports configure/build/clean/run/demo/up/down/status/logs and service management.
- Windows (PowerShell): `./dev.ps1` provides the same commands, plus multi-terminal launches via Windows Terminal or PowerShell.

## Common workflows

Linux/macOS:

```bash
./dev.sh configure
./dev.sh build
./dev.sh demo           # Quick smoke test with mixed protocols  
./dev.sh protocol-test  # Comprehensive test of all protocol combinations
./dev.sh up UAV_1       # Open service, UIs, and UAV_1 in terminals
./dev.sh status         # See PIDs and listening ports
./dev.sh down           # Stop everything and free ports
```

Windows (PowerShell):

```powershell
.\dev.ps1 configure
.\dev.ps1 build
.\dev.ps1 demo
.\dev.ps1 up UAV_1
.\dev.ps1 status
.\dev.ps1 down
```

## Dependencies

- **ZeroMQ**:
  - Linux: `sudo apt-get install -y libzmq3-dev`
  - Windows (vcpkg): `vcpkg install zeromq:x64-windows cppzmq:x64-windows`
- **Boost**:
  - Linux: `sudo apt-get install -y libboost-all-dev`
  - Windows (vcpkg): `vcpkg install boost:x64-windows`

Configure with vcpkg toolchain (optional):
  - `cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake`

ZeroMQ version note:
- Tested with libzmq 4.3.x. Older 4.1+ typically works, but 4.3+ is recommended.

## Troubleshooting

- **Address already in use**:
	- Linux/macOS:
		- `./dev.sh down` to stop any running service, UIs, or simulators.
		- `./dev.sh status` to see listeners on project ports.
		- If needed, kill the PID shown by `ss -ltnp` and retry.
	- Windows:
		- `.\\dev.ps1 down`
		- Use `Get-NetTCPConnection -State Listen | ? { $_.LocalPort -in 5555,5556,5557,5558,5565,5566,5575,5576,... }` and `Stop-Process -Id <PID>` as needed.

- **Windows script execution policy**:
	- If PowerShell blocks scripts, run in the current session: `Set-ExecutionPolicy -Scope Process Bypass`.

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
