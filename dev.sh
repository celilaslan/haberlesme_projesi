#!/usr/bin/env bash
set -euo pipefail

# Fast dev helper for this repo
# Usage:
#   ./dev.sh configure [build_dir]
#   ./dev.sh build [build_dir] [--clean]
#   ./dev.sh run <target> [--] [args...]
#   ./dev.sh demo            # run service → UIs → UAV_1 briefly and show log tail
#   ./dev.sh up              # launch service, UIs, and UAV_1 in separate terminals
#   ./dev.sh down            # stop service/UIs/sims
#   ./dev.sh status          # show running PIDs and port listeners
#   ./dev.sh logs [-f]       # tail or follow telemetry_service log
#   ./dev.sh build-targets [build_dir] <t1> [t2 ...]  # build specific CMake targets
#   ./dev.sh clean [build_dir]  # remove build dir and component executables
#   ./dev.sh rebuild [build_dir]
#   ./dev.sh test            # reserved, if tests exist later
#
# Defaults:
#   build_dir = build
#
# Targets (executables):
#   telemetry_service/telemetry_service
#   uav_sim/uav_sim
#   camera_ui/camera_ui
#   mapping_ui/mapping_ui
#
# Examples:
#   ./dev.sh configure
#   ./dev.sh build
#   ./dev.sh run telemetry_service
#   ./dev.sh run uav_sim
#   ./dev.sh clean

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DEFAULT_BUILD_DIR="${ROOT_DIR}/build"

usage() {
  sed -n '2,40p' "$0"
}

ensure_build_dir() {
  local dir="$1"
  mkdir -p "$dir"
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
  # Collect by process names
  for name in "${names[@]}"; do
    local foundn
    foundn=$(pgrep -x "$name" || true)
    if [[ -n "$foundn" ]]; then pids_set+=" $foundn"; fi
  done
  # Collect by listening ports (common project ports)
  local port_pids
    port_pids=$(ss -ltnp 2>/dev/null | grep -oP 'pid=\K[0-9]+' | sort -u | tr '
' ' ')
  if [[ -n "$port_pids" ]]; then pids_set+=" $port_pids"; fi
  # De-duplicate PIDs
  local all_pids
  # shellcheck disable=SC2206
  all_pids=($(echo "$pids_set" | tr ' ' '\n' | grep -E '^[0-9]+$' | sort -u | tr '\n' ' '))
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
  ss -ltnp | awk 'NR==1 || $4 ~ /:(5555|5557|5558|5559|5565|5569|5575|5579)$/' || true
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
  ensure_build_dir "$build_dir"
  cmake -S "$ROOT_DIR" -B "$build_dir"
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
  # Stop running processes to avoid busy binaries
  kill_existing_procs || true
  if [[ -d "$build_dir" ]]; then
    echo "Cleaning build directory: $build_dir"
    rm -rf "$build_dir"
  fi
  # Remove component executables placed in source directories
  for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
    if [[ -f "$ROOT_DIR/$exe" ]]; then
      echo "Removing $exe"
      rm -f "$ROOT_DIR/$exe"
    fi
  done
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
  echo "Running: $exe $*"
  exec "$exe" "$@"
}

cmd="${1:-}"
case "$cmd" in
  configure) shift; configure "$@" ;;
  build)     shift; build "$@" ;;
  clean)     shift; clean "$@" ;;
  rebuild)   shift; rebuild "$@" ;;
  run)       shift; run "$@" ;;
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
    open_term "camera_ui" "$ROOT_DIR/camera_ui/camera_ui"
    open_term "mapping_ui" "$ROOT_DIR/mapping_ui/mapping_ui"
    # Start provided UAV sims (defaults to UAV_1 if none)
    if [[ $# -eq 0 ]]; then set -- UAV_1; fi
    for uav in "$@"; do
      open_term "uav_sim $uav" "SERVICE_CONFIG=\"$ROOT_DIR/service_config.json\" $ROOT_DIR/uav_sim/uav_sim $uav"
    done
    ;;
  down)
    shift || true
    echo "--- BEFORE ---"; list_procs; list_ports
    kill_existing_procs || true
    # wait briefly for ports to close
    for i in {1..10}; do
      if ss -ltn | awk '$4 ~ /:(5555|5557|5558|5559|5565|5569|5575|5579)$/ {found=1; exit} END{exit !found}'; then
        sleep 0.2
      else
        break
      fi
    done
    echo "--- AFTER ---"; list_ports
    ;;
  demo)
    shift || true
    set -euo pipefail
  trap 'kill_existing_procs || true' EXIT
    # Ensure executables exist
    for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
      if [[ ! -x "$ROOT_DIR/$exe" ]]; then echo "Missing $exe. Run './dev.sh build' first."; exit 2; fi
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
    # Start UIs (briefly)
    (timeout 2s "$ROOT_DIR/camera_ui/camera_ui" || true) &
    (timeout 2s "$ROOT_DIR/mapping_ui/mapping_ui" || true) &
    sleep 0.2
    # Start UAV_1 briefly
    SERVICE_CONFIG="$ROOT_DIR/service_config.json" timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_1 || true
    wait || true
    # Stop service
    kill -INT $svc || true
    wait $svc || true
    # Show log tail
    LOG="$ROOT_DIR/telemetry_service/telemetry_log.txt"
    if [[ -f "$LOG" ]]; then echo "--- LOG TAIL ---"; tail -n 12 "$LOG"; else echo "LOG_NOT_FOUND"; fi
  # Final cleanup of any leftover UIs/sims
  kill_existing_procs || true
    ;;
  test)      echo "No tests configured yet." ;;
  ""|-h|--help|help) usage ;;
  *) echo "Unknown command: $cmd"; usage; exit 1 ;;
 esac
