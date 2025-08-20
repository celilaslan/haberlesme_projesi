#!/bin/bash

# Enhanced development script with code quality tools
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_usage() {
    echo "Enhanced Development Script with Code Quality Tools"
    echo ""
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Build Commands:"
    echo "  build [Debug|Release]     - Build the project (default: Release)"
    echo "  clean                     - Clean build directory"
    echo "  rebuild [Debug|Release]   - Clean and build"
    echo ""
    echo "Code Quality Commands:"
    echo "  format                    - Format all C++ source files using clang-format"
    echo "  lint                      - Run static analysis using clang-tidy"
    echo "  check                     - Run both format check and lint"
    echo ""
    echo "Run Commands:"
    echo "  run <target> [args...]    - Run a built target"
    echo ""
    echo "Available targets:"
    echo "  telemetry_service"
    echo "  uav_sim <UAV_NAME>"
    echo "  camera_ui [--host HOST] [--uav UAV_NAME]"
    echo "  mapping_ui [--host HOST] [--uav UAV_NAME]"
    echo ""
    echo "Examples:"
    echo "  $0 build"
    echo "  $0 format"
    echo "  $0 lint"
    echo "  $0 run telemetry_service"
    echo "  $0 run uav_sim UAV_1"
}

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

check_clang_tools() {
    local missing_tools=()

    if ! command -v clang-format &> /dev/null; then
        missing_tools+=("clang-format")
    fi

    if ! command -v clang-tidy &> /dev/null; then
        missing_tools+=("clang-tidy")
    fi

    if [ ${#missing_tools[@]} -ne 0 ]; then
        log_warning "Missing tools: ${missing_tools[*]}"
        log_info "Install with: sudo apt install clang-format clang-tidy"
        return 1
    fi

    return 0
}

format_code() {
    log_info "Formatting C++ source files..."

    if ! check_clang_tools; then
        log_error "clang-format not available"
        return 1
    fi

    # Find all C++ source files
    local cpp_files=()
    while IFS= read -r -d '' file; do
        cpp_files+=("$file")
    done < <(find "$PROJECT_ROOT" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -not -path "*/build/*" -print0)

    if [ ${#cpp_files[@]} -eq 0 ]; then
        log_warning "No C++ files found to format"
        return 0
    fi

    log_info "Found ${#cpp_files[@]} C++ files to format"

    # Create .clang-format file if it doesn't exist
    if [ ! -f "$PROJECT_ROOT/.clang-format" ]; then
        log_info "Creating .clang-format configuration..."
        cat > "$PROJECT_ROOT/.clang-format" << 'EOF'
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 120
PointerAlignment: Left
ReferenceAlignment: Left
NamespaceIndentation: All
SortIncludes: true
EOF
    fi

    # Format files
    for file in "${cpp_files[@]}"; do
        clang-format -i "$file"
        echo "  Formatted: $(basename "$file")"
    done

    log_success "Code formatting completed"
}

check_format() {
    log_info "Checking code formatting..."

    if ! check_clang_tools; then
        log_error "clang-format not available"
        return 1
    fi

    local cpp_files=()
    while IFS= read -r -d '' file; do
        cpp_files+=("$file")
    done < <(find "$PROJECT_ROOT" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -not -path "*/build/*" -print0)

    local unformatted_files=()
    for file in "${cpp_files[@]}"; do
        if ! clang-format --dry-run --Werror "$file" &> /dev/null; then
            unformatted_files+=("$file")
        fi
    done

    if [ ${#unformatted_files[@]} -eq 0 ]; then
        log_success "All files are properly formatted"
        return 0
    else
        log_error "Found ${#unformatted_files[@]} unformatted files:"
        for file in "${unformatted_files[@]}"; do
            echo "  - $file"
        done
        log_info "Run '$0 format' to fix formatting issues"
        return 1
    fi
}

lint_code() {
    log_info "Running static analysis with clang-tidy..."

    if ! check_clang_tools; then
        log_error "clang-tidy not available"
        return 1
    fi

    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found. Run '$0 build' first."
        return 1
    fi

    if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
        log_error "compile_commands.json not found. Build with CMAKE_EXPORT_COMPILE_COMMANDS=ON"
        return 1
    fi

    # Create .clang-tidy file if it doesn't exist
    if [ ! -f "$PROJECT_ROOT/.clang-tidy" ]; then
        log_info "Creating .clang-tidy configuration..."
        cat > "$PROJECT_ROOT/.clang-tidy" << 'EOF'
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
  -modernize-use-trailing-return-type

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
    fi

    # Find source files to analyze
    local cpp_files=()
    while IFS= read -r -d '' file; do
        cpp_files+=("$file")
    done < <(find "$PROJECT_ROOT" -type f -name "*.cpp" -not -path "*/build/*" -print0)

    if [ ${#cpp_files[@]} -eq 0 ]; then
        log_warning "No C++ source files found to analyze"
        return 0
    fi

    log_info "Analyzing ${#cpp_files[@]} source files..."

    local exit_code=0
    for file in "${cpp_files[@]}"; do
        echo "  Analyzing: $(basename "$file")"
        if ! clang-tidy "$file" -p "$BUILD_DIR" --quiet; then
            exit_code=1
        fi
    done

    if [ $exit_code -eq 0 ]; then
        log_success "Static analysis completed without issues"
    else
        log_warning "Static analysis found issues (see output above)"
    fi

    return $exit_code
}

build_project() {
    local build_type="${1:-Release}"

    log_info "Building project in $build_type mode..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake -DCMAKE_BUILD_TYPE="$build_type" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          ..

    make -j$(nproc)

    log_success "Build completed successfully"
}

clean_project() {
    log_info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    log_success "Clean completed"
}

run_target() {
    local target="$1"
    shift

    case "$target" in
        telemetry_service)
            exec "$BUILD_DIR/telemetry_service/telemetry_service" "$@"
            ;;
        uav_sim)
            if [ $# -eq 0 ]; then
                log_error "UAV name required for uav_sim"
                echo "Usage: $0 run uav_sim <UAV_NAME>"
                exit 1
            fi
            exec "$BUILD_DIR/uav_sim/uav_sim" "$@"
            ;;
        camera_ui)
            exec "$BUILD_DIR/camera_ui/camera_ui" "$@"
            ;;
        mapping_ui)
            exec "$BUILD_DIR/mapping_ui/mapping_ui" "$@"
            ;;
        *)
            log_error "Unknown target: $target"
            print_usage
            exit 1
            ;;
    esac
}

# Main script logic
if [ $# -eq 0 ]; then
    print_usage
    exit 1
fi

command="$1"
shift

case "$command" in
    build)
        build_project "$@"
        ;;
    clean)
        clean_project
        ;;
    rebuild)
        clean_project
        build_project "$@"
        ;;
    format)
        format_code
        ;;
    lint)
        lint_code
        ;;
    check)
        log_info "Running comprehensive code quality check..."
        echo ""
        if check_format && lint_code; then
            log_success "All code quality checks passed!"
        else
            log_error "Code quality checks failed"
            exit 1
        fi
        ;;
    run)
        if [ $# -eq 0 ]; then
            log_error "Target required for run command"
            print_usage
            exit 1
        fi
        run_target "$@"
        ;;
    *)
        log_error "Unknown command: $command"
        print_usage
        exit 1
        ;;
esac
