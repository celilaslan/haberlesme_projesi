# Developer Daily Log

This file records high-level changes and actions performed by the automation to keep a simple end-of-day report.

## 2025-08-13

- Cross-platform fixes (Linux/Windows):
  - Replaced `localtime_s` with portable wrapper: `localtime_s` on Windows, `localtime_r` on POSIX in all components.
  - Telemetry service now reads config path from `SERVICE_CONFIG` env var, defaults to `service_config.json`.
  - UAV simulator uses the same `SERVICE_CONFIG` env var/default path.
- Performance & correctness:
  - Telemetry service receiver loop switched from busy `recv(...dontwait)` to `zmq::poll` with 50 ms timeout.
- UI improvements:
  - Added optional sender mode to `camera_ui` and `mapping_ui`: run with `--send <UAV_NAME>` to forward stdin to service (for UI->UAV command routing tests).
- Cleanup:
  - Removed duplicate `#include <nlohmann/json.hpp>` in telemetry service and added `<cstdlib>` include for `getenv`.
- Smoke test:
  - Built Linux binaries: `ts_service`, `uav_sim_bin`, `camera_ui_bin`, `mapping_ui_bin`.
  - Ran: service + `uav_sim_bin UAV_1` + UIs; verified telemetry published to `camera_UAV_1` and `mapping_UAV_1` and log writes.
- Build system:
  - Added CMake build with per-component executables placed in their own directories and named after the directory:
    - `telemetry_service/telemetry_service`, `uav_sim/uav_sim`, `camera_ui/camera_ui`, `mapping_ui/mapping_ui`.
  - Verified `cmake -S . -B build && cmake --build build -j` produces the expected binaries.
 - Logging robustness:
   - Service now resolves relative `log_file` to the executable directory (telemetry_service folder) and creates parent directories if needed.
   - Receiver poll timeout set to 10 ms; UI forwarder also uses poll with 10 ms timeout.

### Updates (later on 2025-08-13)

- Telemetry service: kept original TCP PUB/PULL design and applied non-invasive fixes only.
  - Portability: guarded `localtime_s`/`localtime_r` usage.
  - Config path: `SERVICE_CONFIG` (fallback `service_config.json`) replaces hardcoded Windows path.
  - Logging: resolve relative `log_file` next to the telemetry_service executable; ensure directories exist.
  - CPU usage: switched both receiver and UI forwarder loops to `zmq::poll` (10 ms).
- Tooling: added a root-level `dev.sh` for fast configure/build/clean/run.
- Docs: updated `README.md` to document `dev.sh` workflow and clarified log path behavior.

### Windows Support (2025-08-13)

- Added `dev.ps1` PowerShell helper mirroring `dev.sh` commands (configure/build/clean/run/demo/up/down/status/logs) using Windows Terminal when available.
- Updated `README.md` with Windows build and run instructions.

### Docs: Troubleshooting and dependencies (2025-08-13)

- README: added Dependencies section (Linux apt package, Windows vcpkg) and a Troubleshooting section for "Address already in use" with commands on both platforms, plus PowerShell execution policy tip.

### Docs: Common workflows and ZeroMQ note (2025-08-13)

- README: added "Common workflows" snippets for Linux/Windows (demo/up/down/status) and a brief ZeroMQ version note (4.3+ recommended) including cppzmq header package hints.

Next:
- Optional CMakeLists.txt to standardize builds (Windows + Linux).
- Add a simple `--log <path>` flag for service to override log file path at runtime.
- Turn service into background daemon/service (systemd on Linux, Windows Service) when ready.

### Service Deployment (2025-08-13)

- Added `scripts/telemetry_service.service`, a `systemd` unit file for running the telemetry service as a background daemon on Linux.
- Added `scripts/install_linux_service.sh` to automate the installation process (build, copy binaries/config, and enable the service).
- Updated `README.md` with instructions for installing and managing the `systemd` service.
- Enhanced `GetTimestamp` function in all C++ files to include millisecond precision for more detailed logging.

### Service Deployment Fix (2025-08-13)

- **Problem**: The `systemd` service was not writing to its log file because of incorrect file paths and permissions.
- **Solution**:
    - The `install_linux_service.sh` script was updated to create a dedicated log directory at `/var/log/telemetry_service`.
    - The script now modifies the installed `service_config.json` to use the absolute log path `/var/log/telemetry_service/telemetry_log.txt`.
    - The `telemetry_service.service` file was updated with `ReadWritePaths` to grant the service explicit permission to write to the new log directory.
- **Result**: The background service now logs correctly to a standard system location.
