# haberlesme_projesi

Cross-platform telemetry service and test apps using ZeroMQ.

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
g++ -std=c++17 telemetry_service/telemetry_service.cpp -lzmq -pthread -o telemetry_service/telemetry_service
g++ -std=c++17 uav_sim/uav_sim.cpp -lzmq -pthread -o uav_sim/uav_sim
g++ -std=c++17 camera_ui/camera_ui.cpp -lzmq -pthread -o camera_ui/camera_ui
g++ -std=c++17 mapping_ui/mapping_ui.cpp -lzmq -pthread -o mapping_ui/mapping_ui
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

```bash
# 1) Start service (reads SERVICE_CONFIG or falls back to service_config.json)
./dev.sh run telemetry_service  # or ./telemetry_service/telemetry_service

# 2) Start UIs first (so they don't miss early PUB messages)
./dev.sh run camera_ui
./dev.sh run mapping_ui

# 3) Start one or more UAV sims
./dev.sh run uav_sim            # or SERVICE_CONFIG=service_config.json ./uav_sim/uav_sim UAV_1
# Optionally start more: UAV_2, UAV_3, etc.

# Optional: send UI commands to a UAV (routes via service)
./camera_ui/camera_ui --send UAV_1
# Then type lines; they will be sent to the service on 5558 as: "UAV_1:[camera-ui] <text>"
```

On Windows (PowerShell):

```powershell
# 1) Start service
.\dev.ps1 run telemetry_service
# 2) Start UIs, then start UAVs
.\dev.ps1 run camera_ui
.\dev.ps1 run mapping_ui
.\dev.ps1 run uav_sim        # optionally pass UAV name like: .\dev.ps1 run uav_sim UAV_1
```

## Config

`service_config.json` defines the UAV endpoints and UI ports. The service writes logs to `log_file`. If relative, logs resolve next to the telemetry_service executable.

Note: With ZeroMQ PUB/SUB, subscribers (UIs) may miss messages sent before they connect and set subscriptions. Starting UIs before UAV sims avoids losing early telemetry.

## Notes

- For Windows builds, link against libzmq and define the same C++17 flags; the code uses `localtime_s` on Windows and `localtime_r` on POSIX.
- The service uses a poll-based receiver loop to avoid busy waiting.

## Orchestration helpers

- Linux/macOS: `./dev.sh` supports configure/build/clean/run/demo/up/down/status/logs.
- Windows (PowerShell): `./dev.ps1` provides the same commands, plus multi-terminal launches via Windows Terminal or PowerShell.

## Common workflows

Linux/macOS:

```bash
./dev.sh configure
./dev.sh build
./dev.sh demo           # quick smoke test (service+UIs+UAV_1)
./dev.sh up UAV_1       # open service, UIs, and UAV_1 in terminals
./dev.sh status         # see PIDs and listening ports
./dev.sh down           # stop everything and free ports
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

- Linux: install ZeroMQ development packages, e.g. Ubuntu/Debian:
	- `sudo apt-get install -y libzmq3-dev`
	- On some distros you may also need the C++ headers (cppzmq): `sudo apt-get install -y cppzmq`
- Windows: install ZeroMQ via vcpkg (recommended):
	- `vcpkg install zeromq:x64-windows cppzmq:x64-windows`
	- Configure with vcpkg toolchain (optional):
		- `cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake`

ZeroMQ version note:

- Tested with libzmq 4.3.x. Older 4.1+ typically works, but 4.3+ is recommended.

## Troubleshooting

- Address already in use:
	- Linux/macOS:
		- `./dev.sh down` to stop any running service, UIs, or simulators.
		- `./dev.sh status` to see listeners on 5555/5557/5558/5559/5565/5569/5575/5579.
		- If needed, kill the PID shown by `ss -ltnp` and retry.
	- Windows:
		- `.\\dev.ps1 down`
		- Use `Get-NetTCPConnection -State Listen | ? { $_.LocalPort -in 5555,5557,5558,5559,5565,5569,5575,5579 }` and `Stop-Process -Id <PID>` as needed.

- Windows script execution policy:
	- If PowerShell blocks scripts, run in the current session: `Set-ExecutionPolicy -Scope Process Bypass`.

## Deployment (Linux Service)

The `telemetry_service` can be installed and run as a `systemd` background service on modern Linux distributions.

1.  **Run the installer with sudo:**
    ```bash
    sudo ./scripts/install_linux_service.sh
    ```
    This script will build the project, copy the executable and configuration to system directories (`/usr/local/bin` and `/etc/telemetry_service`), and set up the `systemd` service.

2.  **Manage the service:**
    ```bash
    # Start the service
    sudo systemctl start telemetry_service

    # Stop the service
    sudo systemctl stop telemetry_service

    # Check the status
    sudo systemctl status telemetry_service

    # View live logs
    sudo journalctl -u telemetry_service -f
    ```

The service is automatically enabled to start on boot. The service's log file is located at `/var/log/telemetry_service/telemetry_log.txt`, and its configuration is managed at `/etc/telemetry_service/service_config.json`.
