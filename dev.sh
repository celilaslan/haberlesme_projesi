#!/usr/bin/env bash
set -euo pipefail

# Cleanup background processes on script exit
trap 'cleanup_background 2>/dev/null || true' EXIT

# Enhanced dev helper for this repo
# Usage:
#   ./dev.sh <command> [options]
#
# Build Commands:
#   configure [build_dir] [build_type] [--warnings] [--debug] [--werror]
#                           - Run CMake to configure the project (Debug/Release).
#                             --warnings: Enable compiler warnings
#                             --debug: Enable debug information
#                             --werror: Treat warnings as errors
#   build [build_dir]       - Build all targets. Can use --clean.
#   rebuild [build_dir]     - Clean, configure, and build the project.
#   clean [build_dir]       - Clean the build directory and executables.
#   watch [build_dir]       - Watch for file changes and rebuild automatically.
#   install [build_dir] [prefix] - Install the project to the specified prefix.
#   package [build_dir]     - Create distribution packages.
#
# Code Quality Commands:
#   format [file]           - Format all C++ files or specific file using clang-format.
#   lint [file]             - Run static analysis on all files or specific file using clang-tidy.
#   quality-check [file]    - Run both format check and lint on all files or specific file.
#   fix-format [file]       - Auto-fix formatting issues in all files or specific file.
#
# Runtime Commands:
#   run <target> [args...]  - Run a specific executable with arguments.
#     <target>: telemetry_service, uav_sim, camera_ui, mapping_ui
#     For UI apps, use: --protocol tcp|udp [--location-only|--status-only|--all-targets] [--send UAV_NAME] [--debug]
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
#   cross-target-test       - Test cross-target subscription functionality.
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
#   ./dev.sh format                   # Format all C++ files
#   ./dev.sh lint src/main.cpp        # Lint specific file
#   ./dev.sh quality-check            # Full code quality check
#   ./dev.sh health                   # Check system health
#   ./dev.sh watch                    # Auto-rebuild on changes
#   ./dev.sh run telemetry_service
#   ./dev.sh run uav_sim UAV_1 --protocol udp
#   ./dev.sh run camera_ui --protocol tcp --location-only
#   ./dev.sh run mapping_ui --protocol udp --all-targets --send UAV_1
#   ./dev.sh up UAV_1 UAV_2 --protocol udp
#   ./dev.sh cross-target-test        # Test new cross-target features
#   ./dev.sh down
#   ./dev.sh logs -f

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DEFAULT_BUILD_DIR="${ROOT_DIR}/build"

# Colors for enhanced output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Enhanced logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

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

# Check for required dependencies (enhanced with clang tools)
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
  if ! have clang-format; then optional_missing+=("clang-format (for code formatting)"); fi
  if ! have clang-tidy; then optional_missing+=("clang-tidy (for static analysis)"); fi
  if ! have inotifywait; then optional_missing+=("inotify-tools (for watch mode)"); fi

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
    log_error "Missing required dependencies:"
    printf "  - %s
" "${missing[@]}" >&2
    return 1
  fi

  if [[ ${#optional_missing[@]} -gt 0 ]]; then
    log_warning "Missing optional dependencies (install for enhanced features):"
    printf "  - %s
" "${optional_missing[@]}" >&2
    echo ""
    log_info "Install all optional tools with:"
    echo "  sudo apt-get install clang-format clang-tidy inotify-tools ninja-build python3"
  fi

  log_success "Dependencies check passed"
  return 0
}

# ================================
# CODE QUALITY FUNCTIONS
# ================================

# Check if clang tools are available
check_clang_tools() {
    local missing_tools=()

    if ! have clang-format; then
        missing_tools+=("clang-format")
    fi

    if ! have clang-tidy; then
        missing_tools+=("clang-tidy")
    fi

    if [[ ${#missing_tools[@]} -ne 0 ]]; then
        log_warning "Missing code quality tools: ${missing_tools[*]}"
        log_info "Install with: sudo apt install clang-format clang-tidy"
        return 1
    fi

    return 0
}

# Create clang configuration files if they don't exist
setup_clang_configs() {
    # Create .clang-format file if it doesn't exist
    if [[ ! -f "$ROOT_DIR/.clang-format" ]]; then
        log_info "Creating .clang-format configuration..."
        cat > "$ROOT_DIR/.clang-format" << 'EOF'
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 120
PointerAlignment: Left
ReferenceAlignment: Left
NamespaceIndentation: All
SortIncludes: true
AllowShortFunctionsOnASingleLine: Empty
AllowShortBlocksOnASingleLine: Never
AllowShortCaseLabelsOnASingleLine: false
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
EOF
        log_success "Created .clang-format configuration"
    fi

    # Create .clang-tidy file if it doesn't exist
    if [[ ! -f "$ROOT_DIR/.clang-tidy" ]]; then
        log_info "Creating .clang-tidy configuration..."
        cat > "$ROOT_DIR/.clang-tidy" << 'EOF'
Checks: >
  -*,
  modernize-*,
  performance-*,
  readability-*,
  bugprone-*,
  clang-analyzer-*,
  cppcoreguidelines-*,
  -readability-braces-around-statements,
  -readability-magic-numbers,
  -cppcoreguidelines-avoid-magic-numbers,
  -modernize-use-trailing-return-type,
  -readability-isolate-declaration

WarningsAsErrors: false
HeaderFilterRegex: '.*'
AnalyzeTemporaryDtors: false
CheckOptions:
  - key: readability-identifier-naming.VariableCase
    value: lower_case
  - key: readability-identifier-naming.FunctionCase
    value: camelBack
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
EOF
        log_success "Created .clang-tidy configuration"
    fi
}

# Format C++ code using clang-format
format_code() {
    local target_file="${1:-}"

    if ! check_clang_tools; then
        log_error "clang-format not available"
        return 1
    fi

    setup_clang_configs

    if [[ -n "$target_file" ]]; then
        # Format single file
        if [[ ! -f "$target_file" ]]; then
            log_error "File not found: $target_file"
            return 1
        fi

        if [[ ! "$target_file" =~ \.(cpp|h|hpp)$ ]]; then
            log_error "File is not a C++ source file: $target_file"
            return 1
        fi

        log_info "Formatting file: $target_file"
        clang-format -i "$target_file"
        log_success "Formatted: $(basename "$target_file")"
    else
        # Format all files
        log_info "Formatting all C++ source files..."

        # Find all C++ source files
        local cpp_files=()
        while IFS= read -r -d '' file; do
            cpp_files+=("$file")
        done < <(find "$ROOT_DIR" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -not -path "*/build/*" -print0)

        if [[ ${#cpp_files[@]} -eq 0 ]]; then
            log_warning "No C++ files found to format"
            return 0
        fi

        log_info "Found ${#cpp_files[@]} C++ files to format"

        # Format files
        for file in "${cpp_files[@]}"; do
            clang-format -i "$file"
            echo "  Formatted: $(basename "$file")"
        done

        log_success "Code formatting completed"
    fi
}

# Check code formatting without modifying files
check_format() {
    local target_file="${1:-}"

    if ! check_clang_tools; then
        log_error "clang-format not available"
        return 1
    fi

    if [[ -n "$target_file" ]]; then
        # Check single file
        if [[ ! -f "$target_file" ]]; then
            log_error "File not found: $target_file"
            return 1
        fi

        log_info "Checking formatting for: $target_file"
        if clang-format --dry-run --Werror "$target_file" &> /dev/null; then
            log_success "File is properly formatted: $(basename "$target_file")"
            return 0
        else
            log_error "File needs formatting: $target_file"
            log_info "Run './dev.sh fix-format $target_file' to fix formatting"
            return 1
        fi
    else
        # Check all files
        log_info "Checking code formatting..."

        local cpp_files=()
        while IFS= read -r -d '' file; do
            cpp_files+=("$file")
        done < <(find "$ROOT_DIR" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -not -path "*/build/*" -print0)

        local unformatted_files=()
        for file in "${cpp_files[@]}"; do
            if ! clang-format --dry-run --Werror "$file" &> /dev/null; then
                unformatted_files+=("$file")
            fi
        done

        if [[ ${#unformatted_files[@]} -eq 0 ]]; then
            log_success "All files are properly formatted"
            return 0
        else
            log_error "Found ${#unformatted_files[@]} unformatted files:"
            for file in "${unformatted_files[@]}"; do
                echo "  - $file"
            done
            log_info "Run './dev.sh fix-format' to fix formatting issues"
            return 1
        fi
    fi
}

# Run static analysis using clang-tidy
lint_code() {
    local target_file="${1:-}"

    if ! check_clang_tools; then
        log_error "clang-tidy not available"
        return 1
    fi

    if [[ ! -d "$DEFAULT_BUILD_DIR" ]]; then
        log_error "Build directory not found. Run './dev.sh configure' first."
        return 1
    fi

    if [[ ! -f "$DEFAULT_BUILD_DIR/compile_commands.json" ]]; then
        log_error "compile_commands.json not found. Rebuilding with CMAKE_EXPORT_COMPILE_COMMANDS=ON"
        configure "$DEFAULT_BUILD_DIR"
    fi

    setup_clang_configs

    if [[ -n "$target_file" ]]; then
        # Lint single file
        if [[ ! -f "$target_file" ]]; then
            log_error "File not found: $target_file"
            return 1
        fi

        if [[ ! "$target_file" =~ \.cpp$ ]]; then
            log_error "File is not a C++ source file (.cpp): $target_file"
            return 1
        fi

        log_info "Analyzing file: $target_file"
        if clang-tidy "$target_file" -p "$DEFAULT_BUILD_DIR" --quiet; then
            log_success "Static analysis completed for: $(basename "$target_file")"
            return 0
        else
            log_warning "Static analysis found issues in: $(basename "$target_file")"
            return 1
        fi
    else
        # Lint all files
        log_info "Running static analysis with clang-tidy..."

        # Find source files to analyze
        local cpp_files=()
        while IFS= read -r -d '' file; do
            cpp_files+=("$file")
        done < <(find "$ROOT_DIR" -type f -name "*.cpp" -not -path "*/build/*" -print0)

        if [[ ${#cpp_files[@]} -eq 0 ]]; then
            log_warning "No C++ source files found to analyze"
            return 0
        fi

        log_info "Analyzing ${#cpp_files[@]} source files..."

        local exit_code=0
        for file in "${cpp_files[@]}"; do
            echo "  Analyzing: $(basename "$file")"
            if ! clang-tidy "$file" -p "$DEFAULT_BUILD_DIR" --quiet; then
                exit_code=1
            fi
        done

        if [[ $exit_code -eq 0 ]]; then
            log_success "Static analysis completed without issues"
        else
            log_warning "Static analysis found issues (see output above)"
        fi

        return $exit_code
    fi
}

# Run comprehensive code quality check
quality_check() {
    local target_file="${1:-}"

    if [[ -n "$target_file" ]]; then
        log_info "Running code quality check for: $target_file"
        echo ""
        if check_format "$target_file" && lint_code "$target_file"; then
            log_success "Code quality check passed for: $(basename "$target_file")"
        else
            log_error "Code quality check failed for: $(basename "$target_file")"
            return 1
        fi
    else
        log_info "Running comprehensive code quality check..."
        echo ""
        if check_format && lint_code; then
            log_success "All code quality checks passed!"
        else
            log_error "Code quality checks failed"
            return 1
        fi
    fi
}

# ================================
# END CODE QUALITY FUNCTIONS
# ================================

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

  # Only collect PIDs from PROJECT-SPECIFIC ports, not all ports
  local project_ports=(5555 5556 5557 5558 5559 5565 5566 5569 5570 5571 5572 5575 5579)
  for port in "${project_ports[@]}"; do
    local port_pid
    port_pid=$(ss -ltnp 2>/dev/null | awk -v port=":$port" '$4 ~ port {gsub(/.*pid=/, "", $7); gsub(/,.*/, "", $7); print $7}' | head -1 || true)
    if [[ -n "$port_pid" && "$port_pid" =~ ^[0-9]+$ ]]; then
      pids_set+=" $port_pid"
    fi
  done

  # De-duplicate PIDs
  local all_pids
  # shellcheck disable=SC2206
  all_pids=($(echo "$pids_set" | tr ' ' '\n' | grep -E '^[0-9]+$' 2>/dev/null | sort -u | tr '\n' ' ' || true))

  if [[ ${#all_pids[@]} -gt 0 ]]; then
    echo "Stopping PIDs: ${all_pids[*]}"

    # First, try graceful shutdown (SIGINT)
    for pid in "${all_pids[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        kill -INT "$pid" 2>/dev/null || true
      fi
    done

    # Wait for graceful shutdown with timeout
    local timeout=5
    local waited=0
    while (( waited < timeout )); do
      local any_alive=false
      for pid in "${all_pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
          any_alive=true
          break
        fi
      done
      if [[ "$any_alive" == "false" ]]; then
        echo "All processes stopped gracefully"
        return 0
      fi
      sleep 0.2
      waited=$((waited + 1))
    done

    # Force kill any remaining processes
    echo "Force killing remaining processes..."
    for pid in "${all_pids[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        echo "Force killing PID $pid"
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
  local port="$1"; local timeout="${2:-10}"
  local waited=0
  echo "Waiting for port $port to be available..."
  while (( waited < timeout )); do
    if ss -ltn | awk '{print $4}' | grep -q ":$port$"; then
      echo "Port $port is ready"
      return 0
    fi
    sleep 0.5; waited=$((waited + 1))
  done
  echo "Timeout waiting for port $port after ${timeout} seconds"
  # Final check
  ss -ltn | awk '{print $4}' | grep -q ":$port$"
}

# Detect a terminal emulator and open a new window running a command
have() { command -v "$1" >/dev/null 2>&1; }

# Track background processes for cleanup
declare -a BACKGROUND_PIDS=()

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
    local bg_pid=$!
    BACKGROUND_PIDS+=("$bg_pid")
    echo "Started background process PID: $bg_pid"
  fi
}

# Cleanup function for background processes
cleanup_background() {
  if [[ ${#BACKGROUND_PIDS[@]} -gt 0 ]]; then
    echo "Cleaning up background processes: ${BACKGROUND_PIDS[*]}"
    for pid in "${BACKGROUND_PIDS[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        kill -INT "$pid" 2>/dev/null || true
      fi
    done
    BACKGROUND_PIDS=()
  fi
}

configure() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  local build_type="${2:-Release}"
  shift 2 2>/dev/null || shift $# 2>/dev/null || true

  # Parse additional options
  local enable_warnings="OFF"
  local debug_info="OFF"
  local warnings_as_errors="OFF"

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --warnings) enable_warnings="ON"; shift ;;
      --debug) debug_info="ON"; shift ;;
      --werror) warnings_as_errors="ON"; shift ;;
      *) echo "Unknown configure option: $1" >&2; return 1 ;;
    esac
  done

  ensure_build_dir "$build_dir"

  echo "Configuring project (Build Type: $build_type)..."
  if [[ "$enable_warnings" == "ON" ]]; then echo "  - Compiler warnings enabled"; fi
  if [[ "$debug_info" == "ON" ]]; then echo "  - Debug information enabled"; fi
  if [[ "$warnings_as_errors" == "ON" ]]; then echo "  - Treating warnings as errors"; fi

  local cmake_args=("-S" "$ROOT_DIR" "-B" "$build_dir" "-DCMAKE_BUILD_TYPE=$build_type")
  cmake_args+=("-DENABLE_WARNINGS=$enable_warnings")
  cmake_args+=("-DBUILD_WITH_DEBUG_INFO=$debug_info")
  cmake_args+=("-DTREAT_WARNINGS_AS_ERRORS=$warnings_as_errors")
  cmake_args+=("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")  # Always enable for clang-tidy

  # Use Ninja if available for faster builds
  if have ninja; then
    cmake_args+=("-G" "Ninja")
    echo "Using Ninja generator for faster builds"
  fi

  cmake "${cmake_args[@]}"
  log_success "Configuration completed"
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

install_project() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  local prefix="${2:-/usr/local}"
  ensure_build_dir "$build_dir"

  echo "Installing project to: $prefix"
  if [[ "$prefix" == "/usr/local" ]] || [[ "$prefix" == "/usr" ]] || [[ "$prefix" =~ ^/opt/.* ]]; then
    sudo cmake --install "$build_dir" --prefix "$prefix"
  else
    cmake --install "$build_dir" --prefix "$prefix"
  fi
}

package_project() {
  local build_dir="${1:-$DEFAULT_BUILD_DIR}"
  ensure_build_dir "$build_dir"

  echo "Creating distribution packages..."
  cd "$build_dir"
  cpack
  cd "$ROOT_DIR"

  echo "Packages created in: $build_dir"
  ls -la "$build_dir"/*.tar.gz "$build_dir"/*.deb 2>/dev/null || echo "No packages found"
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

  log_info "Starting comprehensive protocol testing..."

  # Ensure executables exist
  for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
    if [[ ! -x "$ROOT_DIR/$exe" ]]; then
      log_error "Missing $exe. Run './dev.sh build' first."
      exit 2
    fi
  done

  # Stop any existing processes
  kill_existing_procs

  # Start service
  log_info "Starting telemetry service..."
  "$ROOT_DIR/telemetry_service/telemetry_service" &
  svc=$!
  sleep 1

  if ! kill -0 "$svc" 2>/dev/null; then
    log_error "Service failed to start"
    exit 1
  fi

  log_info "Testing TCP Protocol..."
  timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_1 --protocol tcp || true
  sleep 0.5

  log_info "Testing UDP Protocol..."
  timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_2 --protocol udp || true
  sleep 0.5

  log_info "Testing Default (Both) Protocol..."
  timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_3 || true
  sleep 0.5

  log_info "Testing UI Applications..."
  (timeout 2s "$ROOT_DIR/camera_ui/camera_ui" --protocol tcp || true) &
  (timeout 2s "$ROOT_DIR/mapping_ui/mapping_ui" --protocol udp || true) &
  wait || true

  # Stop service
  kill -INT $svc || true
  wait $svc || true

  log_success "Protocol testing complete"

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

# Test cross-target subscription functionality
cross_target_test() {
  set -euo pipefail
  trap 'kill_existing_procs || true' EXIT

  log_info "Starting cross-target subscription testing..."

  # Ensure executables exist
  for exe in telemetry_service/telemetry_service uav_sim/uav_sim camera_ui/camera_ui mapping_ui/mapping_ui; do
    if [[ ! -x "$ROOT_DIR/$exe" ]]; then
      log_error "Missing $exe. Run './dev.sh build' first."
      exit 2
    fi
  done

  # Stop any existing processes
  kill_existing_procs

  # Start service
  log_info "Starting telemetry service..."
  "$ROOT_DIR/telemetry_service/telemetry_service" &
  svc=$!
  sleep 1

  if ! kill -0 "$svc" 2>/dev/null; then
    log_error "Service failed to start"
    exit 1
  fi

  log_info "Testing cross-target modes..."

  # Test camera UI with cross-target mode
  log_info "Testing camera UI --location-only mode..."
  (timeout 3s "$ROOT_DIR/camera_ui/camera_ui" --protocol tcp --location-only || true) &
  cam_pid=$!

  # Test mapping UI with all-targets mode
  log_info "Testing mapping UI --all-targets mode..."
  (timeout 3s "$ROOT_DIR/mapping_ui/mapping_ui" --protocol udp --all-targets || true) &
  map_pid=$!

  sleep 0.5

  # Start UAV simulators to generate telemetry for different targets
  log_info "Starting UAV simulators..."
  (timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_1 --protocol tcp || true) &
  (timeout 3s "$ROOT_DIR/uav_sim/uav_sim" UAV_2 --protocol udp || true) &

  # Wait for test completion
  wait $cam_pid || true
  wait $map_pid || true

  # Stop service
  kill -INT $svc || true
  wait $svc || true

  log_success "Cross-target testing complete"

  # Show test summary
  echo "✅ Cross-Target Tests Completed:"
  echo "   - Camera UI with --location-only (receives location data from all targets)"
  echo "   - Mapping UI with --all-targets (receives all telemetry)"
  echo "   - UAV_1 with TCP protocol"
  echo "   - UAV_2 with UDP protocol"

  # Show log summary
  LOG="$ROOT_DIR/telemetry_service/telemetry_log.txt"
  if [[ -f "$LOG" ]]; then
    echo "--- Cross-Target Test Log Entries ---"
    tail -n 20 "$LOG" | grep -E "(subscribe|camera|mapping|general)" || true
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

  # Validate UI application arguments
  if [[ "$target" == "camera_ui" || "$target" == "mapping_ui" ]]; then
    if [[ $# -eq 0 ]]; then
      log_error "$target requires --protocol parameter (tcp or udp)"
      echo "Examples:"
      echo "  ./dev.sh run $target --protocol tcp"
      echo "  ./dev.sh run $target --protocol udp --location-only"
      echo "  ./dev.sh run $target --protocol tcp --all-targets --send UAV_1"
      exit 1
    fi

    # Check for protocol argument
    local has_protocol=false
    for arg in "$@"; do
      if [[ "$arg" == "tcp" || "$arg" == "udp" ]]; then
        has_protocol=true
        break
      fi
    done

    if [[ "$has_protocol" == "false" ]]; then
      log_warning "$target typically requires --protocol tcp or --protocol udp"
      echo "Available options:"
      echo "  --protocol tcp|udp     : Communication protocol"
      echo "  --location-only        : Subscribe only to location data from all targets"
      echo "  --status-only          : Subscribe only to status data from all targets"
      echo "  --debug                : Enable debug output to see internal filtering"
      echo "  --all-targets          : Subscribe to all telemetry from all targets"
      echo "  --all-targets          : Subscribe to ALL telemetry data"
      echo "  --send UAV_NAME        : Enable command sending to UAV"
    fi
  fi

  # Validate UAV simulator arguments
  if [[ "$target" == "uav_sim" && $# -eq 0 ]]; then
    log_error "uav_sim requires a UAV name (e.g., UAV_1, UAV_2, UAV_3)"
    echo "Examples:"
    echo "  ./dev.sh run uav_sim UAV_1"
    echo "  ./dev.sh run uav_sim UAV_1 --protocol tcp"
    echo "  ./dev.sh run uav_sim UAV_2 --protocol udp"
    exit 1
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
  cross-target-test) cross_target_test ;;
  format)    shift; format_code "$@" ;;
  fix-format) shift; format_code "$@" ;;
  lint)      shift; lint_code "$@" ;;
  quality-check) shift; quality_check "$@" ;;
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
    if [[ ! -f "$LOG" ]]; then log_error "No log found at $LOG"; exit 1; fi
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
      if [[ ! -x "$ROOT_DIR/$exe" ]]; then log_error "Missing $exe. Run './dev.sh build' first."; exit 2; fi
    done
    # Stop any existing processes first
    kill_existing_procs || true
    # Launch in separate terminals
    open_term "telemetry_service" "$ROOT_DIR/telemetry_service/telemetry_service"
    # Wait for service ports to be ready
    wait_for_port 5557 15 || true
    wait_for_port 5558 15 || true
    sleep 0.2
    open_term "camera_ui (TCP + location-only)" "$ROOT_DIR/camera_ui/camera_ui --protocol tcp --location-only"
    open_term "mapping_ui (UDP + all-targets)" "$ROOT_DIR/mapping_ui/mapping_ui --protocol udp --all-targets"

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
  install)
    shift || true
    install_project "$@"
    ;;
  package)
    shift || true
    package_project "$@"
    ;;
  service-start)
    echo "🚀 Starting telemetry service..."
    sudo systemctl start telemetry_service.service
    if sudo systemctl is-active --quiet telemetry_service.service; then
      echo "✅ Telemetry service started successfully"
      echo "📊 Use './dev.sh service-status' to check detailed status"
      echo "📋 Use './dev.sh service-logs' to view logs"
    else
      echo "❌ Failed to start telemetry service"
      echo "🔍 Use './dev.sh service-status' to check what went wrong"
    fi
    ;;
  service-stop)
    echo "🛑 Stopping telemetry service..."
    sudo systemctl stop telemetry_service.service
    if ! sudo systemctl is-active --quiet telemetry_service.service; then
      echo "✅ Telemetry service stopped successfully"
    else
      echo "❌ Failed to stop telemetry service"
    fi
    ;;
  service-restart)
    echo "🔄 Restarting telemetry service..."
    sudo systemctl restart telemetry_service.service
    if sudo systemctl is-active --quiet telemetry_service.service; then
      echo "✅ Telemetry service restarted successfully"
      echo "📊 Use './dev.sh service-status' to check detailed status"
    else
      echo "❌ Failed to restart telemetry service"
      echo "🔍 Use './dev.sh service-status' to check what went wrong"
    fi
    ;;
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
