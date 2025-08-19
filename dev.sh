#!/usr/bin/env bash
set -euo pipefail

# Fast dev helper for this repo
# Usage:
#   ./dev.sh <command> [options]
#
# Build Commands:
#   configure [build_dir] [build_type] - Run CMake to configure the project (Debug/Release).
#   build [build_dir]       - Build all targets. Can use --clean.
#   rebuild [build_dir]     - Clean, configure, and build the project.
#   clean [build_dir]       - Clean the build directory and executables.
#   watch [build_dir]       - Watch for file changes and rebuild automatically.
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
#   test [build_dir]        - Run project tests or smoke test if no tests available.
#   protocol-test           - Comprehensive test of all protocol combinations.
#   health                  - Comprehensive system health check.
#   logs [-f]               - Show the tail of the service log file (-f to follow).
#
# System Information:
#   info                    - Show environment and project information.
#   deps                    - Check for required dependencies.
#   validate [config_file]  - Validate service configuration file.
#
# Service Commands (require sudo):
#   service-start           - Start the systemd service.
#   service-stop            - Stop the systemd service.
#   service-restart         - Restart the systemd service.
#   service-status          - Check the status of the systemd service.
#   service-logs [-f]       - View the systemd service logs with journalctl.
#
# Defaults:
#   build_dir = build
#   build_type = Release
#
# Examples:
#   ./dev.sh configure build Debug    # Configure for debug build
#   ./dev.sh build
#   ./dev.sh health                   # Check system health
#   ./dev.sh watch                    # Auto-rebuild on changes
#   ./dev.sh run telemetry_service
#   ./dev.sh run uav_sim UAV_1 --protocol udp
#   ./dev.sh run camera_ui --protocol tcp
#   ./dev.sh run mapping_ui --protocol udp
#   ./dev.sh up UAV_1 UAV_2 --protocol udp
#   ./dev.sh down
#   ./dev.sh logs -f

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DEFAULT_BUILD_DIR="${ROOT_DIR}/build"

usage() {
  sed -n '2,60p' "$0"
}

ensure_build_dir() {
  local dir="$1"
  mkdir -p "$dir"
}

# Validate service configuration file
validate_config() {
  local config_file="${1:-$ROOT_DIR/service_config.json}"
  if [[ ! -f "$config_file" ]]; then
    echo "Error: Configuration file not found: $config_file" >&2
    return 1
  fi
  
  # Basic JSON syntax check
  if ! python3 -m json.tool "$config_file" >/dev/null 2>&1; then
    echo "Error: Invalid JSON in configuration file: $config_file" >&2
    return 1
  fi
  
  echo "Configuration file validated: $config_file"
  return 0
}

# Check for required dependencies
check_dependencies() {
  local missing=()
  local optional_missing=()
  
  # Required dependencies
  if ! have cmake; then missing+=("cmake"); fi
  if ! have g++; then 
    if ! have clang++; then missing+=("g++ or clang++"); fi
  fi
  
  # Optional but recommended
  if ! have python3; then optional_missing+=("python3 (for config validation)"); fi
  if ! have ninja; then optional_missing+=("ninja (faster builds)"); fi
  
  # Check for required libraries (more robust check)
  local zmq_found=false
  if pkg-config --exists libzmq 2>/dev/null; then
    zmq_found=true
  elif ldconfig -p | grep -q libzmq 2>/dev/null; then
    zmq_found=true
  elif [[ -f /usr/include/zmq.h ]] || [[ -f /usr/local/include/zmq.h ]]; then
    zmq_found=true
  fi
  
  if [[ "$zmq_found" == "false" ]]; then
    missing+=("libzmq3-dev (install with: sudo apt-get install libzmq3-dev)")
  fi
  
  # Check for Boost libraries
  local boost_found=false
  if pkg-config --exists boost-system 2>/dev/null; then
    boost_found=true
  elif ldconfig -p | grep -q libboost_system 2>/dev/null; then
    boost_found=true
  elif [[ -d /usr/include/boost ]] || [[ -d /usr/local/include/boost ]]; then
    boost_found=true
  fi
  
  if [[ "$boost_found" == "false" ]]; then
    missing+=("libboost-dev (install with: sudo apt-get install libboost-all-dev)")
  fi
  
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Error: Missing required dependencies:" >&2
    printf "  - %s\n" "${missing[@]}" >&2
    return 1
  fi
  
  if [[ ${#optional_missing[@]} -gt 0 ]]; then
    echo "Warning: Missing optional dependencies:" >&2
    printf "  - %s\n" "${optional_missing[@]}" >&2
  fi
  
  echo "Dependencies check passed"
  return 0
}

# Gracefully stop any existing service/UI/sim instances to avoid conflicts
kill_existing_procs() {
  local targets=(
    "telemetry_service/telemetry_service"
    "uav_sim/uav_sim"
    "camera_ui/camera_ui"
    "mapping_ui/mapping_ui"
  )
  local names=("telemetry_service" "uav_sim" "camera_ui" "mapping_ui")
  local pids_set=""
  # Collect by absolute and relative command patterns
  for exe in "${targets[@]}"; do
    for pat in "${ROOT_DIR}/${exe}" "./${exe}" "${exe}"; do
      local found
      found=$(pgrep -f "$pat" || true)
      if [[ -n "$found" ]]; then pids_set+=" $found"; fi
    done
  done
  # Collect by process names (using -f to avoid 15-char limit)
  for name in "${names[@]}"; do
    local foundn
    foundn=$(pgrep -f "$name" || true)
    if [[ -n "$foundn" ]]; then pids_set+=" $foundn"; fi
  done
  # Collect by listening ports (common project ports)
  local port_pids
  port_pids=$(ss -ltnp 2>/dev/null | grep -oP 'pid=\K[0-9]+' 2>/dev/null | sort -u | tr '\n' ' ' || true)
  if [[ -n "$port_pids" ]]; then pids_set+=" $port_pids"; fi
  # De-duplicate PIDs
  local all_pids
  # shellcheck disable=SC2206
  all_pids=($(echo "$pids_set" | tr ' ' '\n' | grep -E '^[0-9]+$' 2>/dev/null | sort -u | tr '\n' ' ' || true))
  if [[ ${#all_pids[@]} -gt 0 ]]; then
    echo "Stopping PIDs: ${all_pids[*]}"
    for pid in "${all_pids[@]}"; do
      kill -INT "$pid" 2>/dev/null || true
    done
    sleep 0.4
    for pid in "${all_pids[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
      fi
    done
  fi
}

list_ports() {
  ss -ltnp | awk 'NR==1 || $4 ~ /:(5555|5556|5557|5558|5559|5565|5569|5575|5579)$/' || true
}

list_procs() {
  ps -eo pid,ppid,cmd | grep -E "(telemetry_service/telemetry_service|uav_sim/uav_sim|camera_ui/camera_ui|mapping_ui/mapping_ui)" | grep -v grep || true
}

wait_for_port() {
  local port="$1"; local timeout="${2:-5}"
  local waited=0
  while (( waited < timeout )); do
    if ss -ltn | awk '{print $4}' | grep -q ":$port$"; then
      return 0
    fi
    sleep 0.2; waited=$((waited+1))
  done
  # Final check
  ss -ltn | awk '{print $4}' | grep -q ":$port$"
}

# Detect a terminal emulator and open a new window running a command
have() { command -v "$1" >/dev/null 2>&1; }

open_term() {
  local title="$1"; shift
  local cmd="$*"
  if have gnome-terminal; then
    gnome-terminal --title="$title" -- bash -lc "$cmd; exec bash"
  elif have konsole; then
    konsole --hold -p tabtitle="$title" -e bash -lc "$cmd"
  elif have xfce4-terminal; then
    xfce4-terminal --title="$title" -e bash -lc "$cmd; bash"
  elif have mate-terminal; then
    mate-terminal --title="$title" -- bash -lc "$cmd; exec bash"
  elif have kitty; then
    kitty --title "$title" bash -lc "$cmd; read -p 'Press enter to close...'"
  elif have alacritty; then
    alacritty -t "$title" -e bash -lc "$cmd; read -p 'Press enter to close...'"
  elif have xterm; then
    xterm -T "$title" -e bash -lc "$cmd; read -p 'Press enter to close...'"
  else
    echo "No supported terminal emulator found; running in background: $cmd"
    bash -lc "$cmd" &
  fi
}

configure() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  local build_type="${2:-Release}"
  ensure_build_dir "$build_dir"
  
  echo "Configuring project (Build Type: $build_type)..."
  local cmake_args=("-S" "$ROOT_DIR" "-B" "$build_dir" "-DCMAKE_BUILD_TYPE=$build_type")
  
  # Use Ninja if available for faster builds
  if have ninja; then
    cmake_args+=("-G" "Ninja")
    echo "Using Ninja generator for faster builds"
  fi
  
  cmake "${cmake_args[@]}"
}

build() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  local clean_first="no"
  shift || true
  if [[ "${1:-}" == "--clean" ]]; then clean_first="yes"; fi
  ensure_build_dir "$build_dir"
  if [[ "$clean_first" == "yes" ]]; then
    cmake --build "$build_dir" --target clean
  fi
  cmake --build "$build_dir" -j $(nproc || echo 4)
}

build_targets() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"; shift || true
  if [[ $# -lt 1 ]]; then echo "Usage: ./dev.sh build-targets [build_dir] <t1> [t2 ...]"; exit 1; fi
  ensure_build_dir "$build_dir"
  cmake -S "$ROOT_DIR" -B "$build_dir"
  cmake --build "$build_dir" --target "$@" -j $(nproc || echo 4)
}

clean() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  echo "Cleaning build directory: ${build_dir}"
  # Stop running processes first
  kill_existing_procs
  # Remove the build directory contents
  if [ -d "$build_dir" ]; then
      rm -rf "${build_dir:?}"/*
  fi
  # Also remove the executables from their source directories
  echo "Removing old executables from source directories..."
  find "${ROOT_DIR}/telemetry_service" -maxdepth 1 -type f -name "telemetry_service" -executable -delete
  find "${ROOT_DIR}/uav_sim" -maxdepth 1 -type f -name "uav_sim" -executable -delete
  find "${ROOT_DIR}/camera_ui" -maxdepth 1 -type f -name "camera_ui" -executable -delete
  find "${ROOT_DIR}/mapping_ui" -maxdepth 1 -type f -name "mapping_ui" -executable -delete
  echo "Clean complete."
}

# Run tests if available
test_project() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  if [[ ! -d "$build_dir" ]]; then
    echo "Build directory not found. Run './dev.sh configure' first." >&2
    return 1
  fi
  
  echo "Running tests..."
  if [[ -f "$build_dir/CTestTestfile.cmake" ]]; then
    cd "$build_dir" && ctest --output-on-failure
  else
    echo "No tests configured in CMake. Running smoke test instead..."
    # Call the demo function internally
    demo_test
  fi
}

# Comprehensive health check
health_check() {
  echo "=== System Health Check ==="
  
  # Check dependencies
  echo "Checking dependencies..."
  if check_dependencies; then
    echo "✓ Dependencies OK"
  else
    echo "✗ Dependencies issues found"
  fi
  
  # Check configuration
  echo "Checking configuration..."
  if validate_config; then
    echo "✓ Configuration OK"
  else
    echo "✗ Configuration issues found"
  fi
  
  # Check build status
  echo "Checking build status..."
  local missing_exes=()
  for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
    if [[ ! -x "$ROOT_DIR/$exe" ]]; then
      missing_exes+=("$exe")
    fi
  done
  
  if [[ ${#missing_exes[@]} -eq 0 ]]; then
    echo "✓ All executables built"
  else
    echo "✗ Missing executables: ${missing_exes[*]}"
    echo "  Run './dev.sh build' to build them"
  fi
  
  # Check running processes
  echo "Checking running processes..."
  if list_procs | grep -q .; then
    echo "✓ Project processes running:"
    list_procs
  else
    echo "ℹ No project processes currently running"
  fi
  
  # Check listening ports
  echo "Checking network ports..."
  if list_ports | tail -n +2 | grep -q .; then
    echo "✓ Project ports in use:"
    list_ports
  else
    echo "ℹ No project ports currently in use"
  fi
  
  echo "=== Health Check Complete ==="
}

# Watch for file changes and rebuild automatically
watch_mode() {
  if ! have inotifywait; then
    echo "Error: inotify-tools not installed. Install with: sudo apt-get install inotify-tools" >&2
    return 1
  fi
  
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  echo "Starting watch mode. Press Ctrl+C to stop."
  echo "Watching for changes in: $ROOT_DIR"
  
  while true; do
    # Watch for changes in source files
    inotifywait -r -e modify,create,delete \
      --include '\.(cpp|h|hpp|c|cc|cxx)$' \
      "$ROOT_DIR" 2>/dev/null || break
    
    echo "Files changed, rebuilding..."
    if build "$build_dir"; then
      echo "Build successful at $(date)"
    else
      echo "Build failed at $(date)"
    fi
    sleep 1  # Debounce rapid changes
  done
}

# Show environment and project information
show_info() {
  echo "=== Project Information ==="
  echo "Project Root: $ROOT_DIR"
  echo "Build Directory: $DEFAULT_BUILD_DIR"
  echo "Current Branch: $(git branch --show-current 2>/dev/null || echo 'Not a git repo')"
  echo "Last Commit: $(git log -1 --oneline 2>/dev/null || echo 'N/A')"
  echo ""
  echo "=== System Information ==="
  echo "OS: $(uname -s) $(uname -r)"
  echo "Architecture: $(uname -m)"
  echo "CPU Cores: $(nproc)"
  echo "Available RAM: $(free -h | awk '/^Mem:/ {print $7}' 2>/dev/null || echo 'N/A')"
  echo ""
  echo "=== Build Tools ==="
  echo "CMake: $(cmake --version | head -1 2>/dev/null || echo 'Not found')"
  echo "GCC: $(gcc --version | head -1 2>/dev/null || echo 'Not found')"
  echo "Ninja: $(ninja --version 2>/dev/null || echo 'Not found')"
  echo "Python: $(python3 --version 2>/dev/null || echo 'Not found')"
  echo ""
  echo "=== Configuration ==="
  if [[ -f "$ROOT_DIR/service_config.json" ]]; then
    echo "Service Config: Found"
    local uav_count=$(python3 -c "import json; print(len(json.load(open('$ROOT_DIR/service_config.json'))['uavs']))" 2>/dev/null || echo "Unknown")
    echo "Configured UAVs: $uav_count"
  else
    echo "Service Config: Not found"
  fi
}

# Run a comprehensive demo/smoke test
demo_test() {
  set -euo pipefail
  trap 'kill_existing_procs || true' EXIT
  
  # Ensure executables exist
  for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
    if [[ ! -x "$ROOT_DIR/$exe" ]]; then 
      echo "Missing $exe. Run './dev.sh build' first."; 
      exit 2; 
    fi
  done
  
  # Stop any existing service/UIs/sims to avoid address-in-use
  kill_existing_procs
  
  # Show current listeners for visibility
  ss -ltnp | awk 'NR==1 || $4 ~ /:(5555|5557|5558|5559|5565|5569|5575|5579)$/' || true
  
  # Start service
  "$ROOT_DIR/telemetry_service/telemetry_service" &
  svc=$!
  sleep 0.6
  
  if ! kill -0 "$svc" 2>/dev/null; then
    echo "Service failed to start (likely port in use). Current listeners:"
    ss -ltnp | awk 'NR==1 || $4 ~ /:(5555|5557|5558|5559|5565|5569|5575|5579)$/' || true
    exit 1
  fi
  
  # Start UIs (briefly) - Note: UIs now require explicit protocol selection
  (timeout 2s "$ROOT_DIR/camera_ui/camera_ui" --protocol tcp || true) &
  (timeout 2s "$ROOT_DIR/mapping_ui/mapping_ui" --protocol udp || true) &
  sleep 0.2
  
  # Start UAV_1 briefly
  SERVICE_CONFIG="$ROOT_DIR/service_config.json" timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_1 || true
  wait || true
  
  # Stop service
  kill -INT $svc || true
  wait $svc || true
  
  # Show log tail
  LOG="$ROOT_DIR/telemetry_service/telemetry_log.txt"
  if [[ -f "$LOG" ]]; then 
    echo "--- LOG TAIL ---"
    tail -n 12 "$LOG"
  else 
    echo "LOG_NOT_FOUND"
  fi
  
  # Final cleanup of any leftover UIs/sims
  kill_existing_procs || true
}

# Comprehensive protocol testing
protocol_test() {
  set -euo pipefail
  trap 'kill_existing_procs || true' EXIT
  
  echo "=== Comprehensive Protocol Testing ==="
  
  # Ensure executables exist
  for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
    if [[ ! -x "$ROOT_DIR/$exe" ]]; then 
      echo "Missing $exe. Run './dev.sh build' first."; 
      exit 2; 
    fi
  done
  
  # Stop any existing processes
  kill_existing_procs
  
  # Start service
  echo "Starting telemetry service..."
  "$ROOT_DIR/telemetry_service/telemetry_service" &
  svc=$!
  sleep 1
  
  if ! kill -0 "$svc" 2>/dev/null; then
    echo "Service failed to start"
    exit 1
  fi
  
  echo "Testing TCP Protocol..."
  timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_1 --protocol tcp || true
  sleep 0.5
  
  echo "Testing UDP Protocol..."
  timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_2 --protocol udp || true
  sleep 0.5
  
  echo "Testing Default (Both) Protocol..."
  timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_3 || true
  sleep 0.5
  
  echo "Testing UI Applications..."
  (timeout 2s "$ROOT_DIR/camera_ui/camera_ui" --protocol tcp || true) &
  (timeout 2s "$ROOT_DIR/mapping_ui/mapping_ui" --protocol udp || true) &
  wait || true
  
  # Stop service
  kill -INT $svc || true
  wait $svc || true
  
  echo "=== Protocol Testing Complete ==="
  
  # Show test summary
  echo "✅ Tests Completed:"
  echo "   - TCP Protocol (UAV_1)"
  echo "   - UDP Protocol (UAV_2)" 
  echo "   - Default Both Protocols (UAV_3)"
  echo "   - Camera UI with TCP"
  echo "   - Mapping UI with UDP"
  
  # Show log summary
  LOG="$ROOT_DIR/telemetry_service/telemetry_log.txt"
  if [[ -f "$LOG" ]]; then 
    echo "--- Recent Log Entries ---"
    tail -n 15 "$LOG" | grep -E "(TCP|UDP|UAV_[1-3])" || true
  fi
  
  # Final cleanup
  kill_existing_procs || true
}

rebuild() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  clean "$build_dir"
  configure "$build_dir"
  build "$build_dir"
}

run() {
  local target="${1:-}"; shift || true
  if [[ -z "$target" ]]; then echo "run requires a target"; exit 1; fi
  # Map target names to executable paths
  local exe=""
  case "$target" in
    telemetry_service) exe="${ROOT_DIR}/telemetry_service/telemetry_service" ;;
    uav_sim)           exe="${ROOT_DIR}/uav_sim/uav_sim" ;;
    camera_ui)         exe="${ROOT_DIR}/camera_ui/camera_ui" ;;
    mapping_ui)        exe="${ROOT_DIR}/mapping_ui/mapping_ui" ;;
    *) echo "Unknown target: $target"; exit 1 ;;
  esac
  if [[ ! -x "$exe" ]]; then
    echo "Executable not found: $exe" >&2
    echo "Hint: run './dev.sh build' first." >&2
    exit 2
  fi
  # Allow passing args after --
  if [[ "${1:-}" == "--" ]]; then shift; fi
  
  # Show usage hint for UI applications
  if [[ "$target" == "camera_ui" || "$target" == "mapping_ui" ]] && [[ $# -eq 0 ]]; then
    echo "Note: $target requires --protocol parameter (tcp or udp)"
    echo "Example: ./dev.sh run $target --protocol tcp"
  fi
  
  echo "Running: $exe $*"
  "$exe" "$@"
}

cmd="${1:-}"
case "$cmd" in
  configure) shift; configure "$@" ;;
  build)     shift; build "$@" ;;
  clean)     shift; clean "$@" ;;
  rebuild)   shift; rebuild "$@" ;;
  run)       shift; run "$@" ;;
  test)      shift; test_project "$@" ;;
  protocol-test) protocol_test ;;
  watch)     shift; watch_mode "$@" ;;
  health)    health_check ;;
  info)      show_info ;;
  deps)      check_dependencies ;;
  validate)  shift; validate_config "$@" ;;
  status)
    echo "--- PROCESSES ---"; list_procs; echo "--- PORTS ---"; list_ports ;;
  logs)
    shift || true
    LOG="$ROOT_DIR/telemetry_service/telemetry_log.txt"
    if [[ ! -f "$LOG" ]]; then echo "No log found at $LOG"; exit 1; fi
    if [[ "${1:-}" == "-f" ]]; then
      tail -n 20 -f "$LOG"
    else
      tail -n 100 "$LOG"
    fi ;;
  build-targets)
    shift; build_targets "$@" ;;
  up)
    shift || true
    # Ensure executables exist
    for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
      if [[ ! -x "$ROOT_DIR/$exe" ]]; then echo "Missing $exe. Run './dev.sh build' first."; exit 2; fi
    done
    # Stop any existing processes first
    kill_existing_procs || true
    # Launch in separate terminals
    open_term "telemetry_service" "$ROOT_DIR/telemetry_service/telemetry_service"
    # Wait for service ports to be ready
    wait_for_port 5557 15 || true
    wait_for_port 5558 15 || true
    sleep 0.2
    open_term "camera_ui (TCP)" "$ROOT_DIR/camera_ui/camera_ui --protocol tcp"
    open_term "mapping_ui (UDP)" "$ROOT_DIR/mapping_ui/mapping_ui --protocol udp"
    
    # Separate UAV names from other arguments
    local uav_names=()
    local uav_args=()
    while [[ $# -gt 0 ]]; do
      case "$1" in
        UAV_*)
          uav_names+=("$1")
          shift
          ;;
        *)
          uav_args+=("$1")
          shift
          ;;
      esac
    done

    # If no UAV names provided, default to UAV_1
    if [[ ${#uav_names[@]} -eq 0 ]]; then
      uav_names=("UAV_1")
    fi

    # Start provided UAV sims with any extra arguments
    for uav in "${uav_names[@]}"; do
      # Note: uav_args are intentionally unquoted to allow shell parsing of multiple args
      open_term "uav_sim $uav" "SERVICE_CONFIG=\"$ROOT_DIR/service_config.json\" $ROOT_DIR/uav_sim/uav_sim $uav ${uav_args[*]}"
    done
    ;;
  down)
    shift || true
    echo "--- BEFORE ---"; list_procs; list_ports
    kill_existing_procs || true
    # wait briefly for ports to close
    for i in {1..10}; do
      if ss -ltn | awk '$4 ~ /:(5555|5556|5557|5558|5559|5565|5569|5575|5579)$/ {found=1; exit} END{exit !found}'; then
        sleep 0.2
      else
        break
      fi
    done
    echo "--- AFTER ---"; list_ports
    ;;
  demo)
    shift || true
    demo_test
    ;;
  service-start) sudo systemctl start telemetry_service.service ;;
  service-stop) sudo systemctl stop telemetry_service.service ;;
  service-restart) sudo systemctl restart telemetry_service.service ;;
  service-status) sudo systemctl status telemetry_service.service ;;
  service-logs)
    shift || true
    if [[ "${1:-}" == "-f" ]]; then
      sudo journalctl -u telemetry_service.service -f
    else
      sudo journalctl -u telemetry_service.service -n 100 --no-pager
    fi
    ;;
  ""|-h|--help|help) usage ;;
  *) echo "Unknown command: $cmd"; usage; exit 1 ;;
 esac
