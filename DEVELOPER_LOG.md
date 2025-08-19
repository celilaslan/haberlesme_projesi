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

### UDP Telemetry Support (2025-08-13)

- **Goal**: Added a parallel telemetry channel using UDP (Boost.Asio) alongside the existing ZeroMQ (TCP) channel.
- **Configuration**:
    - Added `udp_telemetry_port` to `service_config.json`.
    - Updated `CMakeLists.txt` to find and link the Boost.Asio and Boost.System libraries.
- **Telemetry Service**:
    - Refactored to launch a `UdpServer` in a separate thread.
    - The `UdpServer` uses a `boost::asio::async_receive_from` loop to listen for UDP packets without blocking.
    - A new `ProcessAndPublishTelemetry` function was created to handle message processing from both ZMQ and UDP sources, ensuring all telemetry is published via the single ZMQ PUB socket to the UIs.
- **UAV Simulator**:
    - Added a `--protocol udp` command-line flag.
    - When specified, the simulator sends telemetry via a UDP socket to the port defined in the config.
    - The UDP message payload is prefixed with the UAV's name (e.g., `"UAV_1:..."`) so the service can identify the sender.
- **Bug Fixes**:
    - Resolved a `boost::system::system_error` ("Invalid argument") in the `uav_sim` by using a `udp::resolver` to correctly handle hostnames (like "localhost") from the config file.
    - Improved the `dev.sh` script's `clean` command to remove old executables, preventing stale builds from being run.
- **Result**: The system now supports both ZMQ and UDP for telemetry ingestion, making it more flexible. The service acts as a bridge, normalizing data from different protocols before publishing it to clients.

### Tooling and Refactoring (2025-08-13)

- **Goal**: Improve developer experience by centralizing service management and reorganizing project files.
- **File Structure**:
    - Moved `install_linux_service.sh` to the project root.
    - Moved `telemetry_service.service` into the `telemetry_service` directory.
    - Removed the now-empty `scripts` directory.
- **`dev.sh` Enhancements**:
    - Integrated `systemd` management commands directly into the script.
    - New commands: `service-start`, `service-stop`, `service-restart`, `service-status`, and `service-logs [-f]`.
    - These commands wrap `systemctl` and `journalctl` for convenient access.
- **Documentation**:
    - Updated `README.md` to reflect the new installation script path and the new `dev.sh` service management commands.

## 2025-08-15

### Per-UAV UDP Telemetry Ports

- **Goal**: Replace single global UDP server with dedicated UDP servers per UAV for better network isolation and security.
- **Configuration Changes**:
    - Added `udp_telemetry_port` field to each UAV configuration in `service_config.json`.
    - Removed global `udp_telemetry_port` from service configuration.
    - Updated UAV configurations: UAV_1:5556, UAV_2:5566, UAV_3:5576.
- **Telemetry Service Refactoring**:
    - Modified `UAVConfig` struct to include per-UAV `udp_telemetry_port`.
    - Removed global UDP port from `ServiceConfig`.
    - Updated `UdpServer` class to bind to specific IP addresses instead of `0.0.0.0`.
    - Enhanced constructor to accept UAV name, IP, and port for targeted binding.
    - Improved logging to show individual UDP server endpoints and UAV associations.
    - Created multiple `UdpServer` instances - one per UAV configuration.
- **UAV Simulator Updates**:
    - Modified config loading to read UDP port from UAV's own configuration.
    - Removed dependency on global UDP port configuration.
    - Enhanced error handling for missing per-UAV UDP ports.
- **Benefits Achieved**:
    - **Network Isolation**: Each UAV has dedicated UDP endpoint.
    - **Better Security**: Individual access control per UAV possible.
    - **Fault Isolation**: UDP issues with one UAV don't affect others.
    - **Flexibility**: UAVs can be on different network interfaces.
    - **Easier Troubleshooting**: Clear endpoint identification in logs.
- **Documentation Updates**:
    - Updated `README.md` with new configuration format and architecture details.
    - Added troubleshooting port ranges to include new UDP ports.
    - Enhanced deployment section to mention per-UAV UDP capabilities.

## 2025-08-18

### Enhanced Logging System

- **Goal**: Improve logging with structured, level-based output that works better with both console and systemd journald.
- **Logger Class Enhancements**:
    - **Added Log Levels**: DEBUG, INFO, WARN, ERROR with filtering capabilities.
    - **Structured Logging**: Component-based prefixes (`[ZMQ]`, `[UDP]`, `[SERVICE]`) for better organization.
    - **New Methods**: 
        - `Logger::debug()`, `Logger::warn()` for additional log levels
        - `Logger::status(component, action, details)` for structured status messages
        - `Logger::metric(name, value, unit)` for performance metrics
        - `Logger::serviceStarted()` for comprehensive startup summary
        - `Logger::setLevel()` for runtime log level control
    - **Enhanced Features**:
        - Level-based filtering to reduce noise in production
        - Consistent timestamp format with millisecond precision
        - Better error context and structured information
- **Service Startup Improvements**:
    - **Comprehensive Summary**: Shows UAV count, all ZMQ ports, all UDP ports
    - **Step-by-step Initialization**: Clear component binding status
    - **Service Health**: Better visibility into service state
    - **Before**: `=== SERVICE STARTED ===` / `All services running.`
    - **After**: 
        ```
        [2025-08-18 08:02:05.224] INFO: [SERVICE] STARTING (Multi-UAV Telemetry Service v1.0)
        [2025-08-18 08:02:05.225] INFO: === SERVICE STARTUP COMPLETE ===
        [2025-08-18 08:02:05.225] INFO: Configuration Summary:
        [2025-08-18 08:02:05.225] INFO:   UAVs configured: 3
        [2025-08-18 08:02:05.225] INFO:   ZMQ ports: 5555, 5559, 5565, 5569, 5575, 5579, 5557, 5558
        [2025-08-18 08:02:05.225] INFO:   UDP ports: 5556, 5566, 5576
        [2025-08-18 08:02:05.225] INFO: Service ready for connections.
        ```
- **Shutdown Sequence Enhancement**:
    - **Graceful Steps**: Clear component shutdown order with status updates
    - **Before**: `Shutdown signal received` / `=== SERVICE SHUTDOWN COMPLETED ===`
    - **After**:
        ```
        [2025-08-18 07:57:37.943] INFO: [SERVICE] SHUTTING DOWN (Signal received)
        [2025-08-18 07:57:37.943] INFO: [UDP] STOPPING (Shutting down UDP services)
        [2025-08-18 07:57:37.944] INFO: [ZMQ] STOPPING (Shutting down ZMQ services)
        [2025-08-18 07:57:37.945] INFO: [ZMQ] Forwarder thread stopped
        [2025-08-18 07:57:37.945] INFO: [ZMQ] Receiver thread stopped
        [2025-08-18 07:57:37.946] INFO: [SERVICE] SHUTDOWN COMPLETE (All services stopped gracefully)
        ```
- **Component-Specific Logging**:
    - **ZmqManager**: Updated to use structured status messages for bind operations and thread lifecycle
    - **UdpManager**: Enhanced server binding messages with clear component identification
    - **TelemetryService**: Improved startup and shutdown flow with detailed progress reporting
- **Benefits Achieved**:
    - **Console Usage**: Clear structure, visual hierarchy, easy component identification
    - **systemd/journald**: Better integration with `journalctl`, structured metadata for filtering
    - **Operations**: Component-based filtering for troubleshooting, clear status indicators
    - **Development**: Debug level control, reduced noise in production
    - **Monitoring**: Clear startup/shutdown states for automated monitoring
- **Backward Compatibility**: All existing `Logger::info()` and `Logger::error()` calls preserved
- **Service Update Workflow**: Established efficient update process without full service reinstallation:
    - Build changes → Stop service → Run install script → Start service
    - No need to delete/recreate systemd service for code updates

## 2025-08-19

### Protocol Architecture Modernization

- **Backward Compatibility Removal**:
    - Eliminated all legacy configuration field name support (e.g., old `telemetry_port` vs new `tcp_telemetry_port`)
    - Cleaned up Config.cpp from all backward compatibility checks
    - Modern configuration enforced throughout codebase

- **Protocol Equality Implementation**:
    - Removed TCP default preference bias
    - UAV simulator supports three explicit modes: `--protocol tcp`, `--protocol udp`, or default (both)
    - UI applications require explicit protocol selection: `--protocol tcp|udp` (no defaults)
    - Professional approach eliminating protocol assumptions

- **Clean Architecture Separation**:
    - Implemented protocol-specific routing in TelemetryService
    - ZmqManager handles TCP data flows only
    - UdpManager handles UDP data flows only  
    - Eliminated cross-protocol contamination in service routing
    - Updated `processAndPublishTelemetry()` to include protocol parameter

- **Development Script Updates**:
    - Enhanced `dev.sh` with new `protocol-test` command for comprehensive testing
    - Fixed demo and up commands to include required protocol parameters
    - Added helpful hints for UI applications when protocol not specified
    - Updated `dev.ps1` with same improvements for Windows
    - Updated README.md with correct usage examples and protocol requirements

- **Testing Infrastructure**:
    - Comprehensive protocol testing validates TCP-only, UDP-only, and dual-protocol modes
    - Verified clean separation between protocol managers
    - All components tested with proper protocol selection
    - Professional development workflow established
