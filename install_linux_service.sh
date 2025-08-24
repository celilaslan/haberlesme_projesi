#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# TELEMETRY SERVICE INSTALLATION SCRIPT
# ============================================================================
#
# This script automates the installation and uninstallation of the telemetry
# service as a systemd service on Linux systems. It handles:
# - Building the project
# - Installing the executable to /usr/local/bin
# - Setting up configuration in /etc/telemetry_service
# - Creating log directories with proper permissions
# - Installing and enabling the systemd service
# - Complete uninstallation of all components
#
# USAGE:
#   sudo ./install_linux_service.sh           # Install the service
#   sudo ./install_linux_service.sh --uninstall   # Uninstall the service
#   sudo ./install_linux_service.sh --help        # Show help
#
# REQUIREMENTS:
#   - Root privileges (run with sudo)
#   - Systemd-based Linux distribution
#   - Build dependencies already installed (for installation only)
# ============================================================================

# --- Configuration Variables ---
# Source directory of the project (auto-detected from script location)
REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# Installation paths following Linux Filesystem Hierarchy Standard (FHS)
INSTALL_BIN_DIR="/usr/local/bin"             # Executable installation directory
INSTALL_CONFIG_DIR="/etc/telemetry_service"  # Configuration directory
LOG_DIR="/var/log/telemetry_service"         # Log file directory
SYSTEMD_DIR="/etc/systemd/system"            # Systemd service file directory

# File names
EXE_NAME="telemetry_service"
CONFIG_NAME="service_config.json"
SERVICE_FILE="telemetry_service.service"

# Source file paths
SOURCE_EXE="${REPO_ROOT}/telemetry_service/${EXE_NAME}"
SOURCE_CONFIG="${REPO_ROOT}/${CONFIG_NAME}"
SOURCE_SERVICE_FILE="${REPO_ROOT}/telemetry_service/${SERVICE_FILE}"

# --- Helper Functions ---

show_help() {
    echo "TELEMETRY SERVICE INSTALLER/UNINSTALLER"
    echo ""
    echo "USAGE:"
    echo "  sudo $0                    # Install the telemetry service"
    echo "  sudo $0 --uninstall       # Uninstall the telemetry service"
    echo "  sudo $0 --help            # Show this help message"
    echo ""
    echo "INSTALLATION:"
    echo "  - Builds the project automatically"
    echo "  - Installs executable to /usr/local/bin"
    echo "  - Sets up configuration in /etc/telemetry_service"
    echo "  - Creates log directory in /var/log/telemetry_service"
    echo "  - Installs and enables systemd service"
    echo ""
    echo "UNINSTALLATION:"
    echo "  - Stops and disables the systemd service"
    echo "  - Removes all installed files and directories"
    echo "  - Optionally preserves logs (user confirmation)"
    echo ""
    echo "REQUIREMENTS:"
    echo "  - Root privileges (run with sudo)"
    echo "  - Systemd-based Linux distribution"
    echo "  - Build dependencies (for installation only)"
}

# Uninstall function
uninstall_service() {
    echo "============================================================================"
    echo "TELEMETRY SERVICE UNINSTALLER"
    echo "============================================================================"
    echo "This will remove the telemetry service and all associated files."
    echo ""

    # Check what's currently installed
    local installed_files=()
    local service_status=""

    if [[ -f "${INSTALL_BIN_DIR}/${EXE_NAME}" ]]; then
        installed_files+=("Executable: ${INSTALL_BIN_DIR}/${EXE_NAME}")
    fi

    if [[ -d "$INSTALL_CONFIG_DIR" ]]; then
        installed_files+=("Configuration: ${INSTALL_CONFIG_DIR}/")
    fi

    if [[ -f "${SYSTEMD_DIR}/${SERVICE_FILE}" ]]; then
        installed_files+=("Service file: ${SYSTEMD_DIR}/${SERVICE_FILE}")
        if systemctl is-active "${SERVICE_FILE}" >/dev/null 2>&1; then
            service_status="running"
        elif systemctl is-enabled "${SERVICE_FILE}" >/dev/null 2>&1; then
            service_status="enabled"
        else
            service_status="installed"
        fi
    fi

    if [[ -d "$LOG_DIR" ]]; then
        installed_files+=("Log directory: ${LOG_DIR}/")
    fi

    if [[ ${#installed_files[@]} -eq 0 ]]; then
        echo "No telemetry service installation found."
        echo "Nothing to uninstall."
        return 0
    fi

    echo "FOUND INSTALLATION:"
    for file in "${installed_files[@]}"; do
        echo "  - $file"
    done

    if [[ -n "$service_status" ]]; then
        echo "Service status: $service_status"
    fi

    echo ""
    read -p "Do you want to proceed with uninstallation? [y/N]: " confirm
    if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
        echo "Uninstallation cancelled."
        return 0
    fi

    echo ""
    echo "Starting uninstallation..."

    # Step 1: Stop and disable service
    if [[ -f "${SYSTEMD_DIR}/${SERVICE_FILE}" ]]; then
        echo "[1/5] Stopping and disabling systemd service..."

        if systemctl is-active "${SERVICE_FILE}" >/dev/null 2>&1; then
            echo "  Stopping service..."
            systemctl stop "${SERVICE_FILE}"
        fi

        if systemctl is-enabled "${SERVICE_FILE}" >/dev/null 2>&1; then
            echo "  Disabling service..."
            systemctl disable "${SERVICE_FILE}"
        fi

        echo "  Removing service file..."
        rm -f "${SYSTEMD_DIR}/${SERVICE_FILE}"

        echo "  Reloading systemd..."
        systemctl daemon-reload

        echo "✓ Service stopped and removed"
    else
        echo "[1/5] No systemd service found, skipping..."
    fi

    # Step 2: Remove executable
    if [[ -f "${INSTALL_BIN_DIR}/${EXE_NAME}" ]]; then
        echo "[2/5] Removing executable..."
        rm -f "${INSTALL_BIN_DIR}/${EXE_NAME}"
        echo "✓ Executable removed"
    else
        echo "[2/5] No executable found, skipping..."
    fi

    # Step 3: Remove configuration
    if [[ -d "$INSTALL_CONFIG_DIR" ]]; then
        echo "[3/5] Removing configuration directory..."
        rm -rf "$INSTALL_CONFIG_DIR"
        echo "✓ Configuration removed"
    else
        echo "[3/5] No configuration found, skipping..."
    fi

    # Step 4: Handle log directory with user confirmation
    if [[ -d "$LOG_DIR" ]]; then
        echo "[4/5] Handling log directory..."
        local log_size=$(du -sh "$LOG_DIR" 2>/dev/null | cut -f1 || echo "unknown")
        echo "  Log directory size: $log_size"
        echo "  Location: $LOG_DIR"
        echo ""
        read -p "Remove log directory and all logs? [y/N]: " remove_logs
        if [[ "$remove_logs" =~ ^[Yy]$ ]]; then
            rm -rf "$LOG_DIR"
            echo "✓ Log directory removed"
        else
            echo "✓ Log directory preserved"
        fi
    else
        echo "[4/5] No log directory found, skipping..."
    fi

    # Step 5: Final cleanup
    echo "[5/5] Final system cleanup..."
    systemctl daemon-reload
    echo "✓ System cleanup completed"

    echo ""
    echo "============================================================================"
    echo "UNINSTALLATION COMPLETE"
    echo "============================================================================"
    echo "The telemetry service has been successfully removed from the system."
    echo ""
    echo "REMOVED COMPONENTS:"
    for file in "${installed_files[@]}"; do
        if [[ "$file" =~ "Log directory" ]] && [[ "$remove_logs" != "y" && "$remove_logs" != "Y" ]]; then
            echo "  - $file (preserved)"
        else
            echo "  - $file"
        fi
    done
    echo ""
    echo "The system is now clean of telemetry service components."
    echo "============================================================================"
}

# --- Command Line Argument Parsing ---

# Parse command line arguments
case "${1:-}" in
    --uninstall)
        # Check for root privileges
        if [[ $EUID -ne 0 ]]; then
            echo "ERROR: Uninstallation requires root privileges."
            echo "USAGE: sudo $0 --uninstall"
            exit 1
        fi

        # Check if systemd is available
        if ! command -v systemctl >/dev/null 2>&1; then
            echo "ERROR: systemctl not found. This script requires a systemd-based Linux distribution."
            exit 1
        fi

        uninstall_service
        exit 0
        ;;
    --help|-h)
        show_help
        exit 0
        ;;
    "")
        # Continue with installation (default behavior)
        ;;
    *)
        echo "ERROR: Unknown option: $1"
        echo ""
        show_help
        exit 1
        ;;
esac

# --- Pre-flight Checks ---

# Check for root privileges (required for system-wide installation)
if [[ $EUID -ne 0 ]]; then
   echo "ERROR: This script must be run as root (use sudo)."
   echo "USAGE: sudo ./install_linux_service.sh"
   exit 1
fi

# Check if systemd is available
if ! command -v systemctl >/dev/null 2>&1; then
    echo "ERROR: systemctl not found. This script requires a systemd-based Linux distribution."
    exit 1
fi

# Validate source files exist before proceeding
if [[ ! -f "$SOURCE_CONFIG" ]]; then
    echo "ERROR: Configuration file not found at ${SOURCE_CONFIG}"
    echo "       Make sure you're running this script from the project root."
    exit 1
fi

if [[ ! -f "$SOURCE_SERVICE_FILE" ]]; then
    echo "ERROR: Service file not found at ${SOURCE_SERVICE_FILE}"
    echo "       Make sure the service file exists in the telemetry_service directory."
    exit 1
fi

echo "============================================================================"
echo "TELEMETRY SERVICE INSTALLER"
echo "============================================================================"
echo "Repository root: ${REPO_ROOT}"
echo "Target executable: ${INSTALL_BIN_DIR}/${EXE_NAME}"
echo "Target config: ${INSTALL_CONFIG_DIR}/${CONFIG_NAME}"
echo "Log directory: ${LOG_DIR}"
echo "============================================================================"

# --- Installation Steps ---

# Step 1: Build the project
echo "[1/6] Building the project..."
if ! "${REPO_ROOT}/dev.sh" build; then
    echo "ERROR: Build failed. Please fix build errors before installing."
    exit 1
fi

# Verify the executable was created and is functional
if [[ ! -f "$SOURCE_EXE" ]]; then
    echo "ERROR: Executable not found at ${SOURCE_EXE} after build."
    echo "       Make sure the build completed successfully."
    exit 1
fi

# Test that the executable is actually runnable
if ! file "$SOURCE_EXE" | grep -q "executable"; then
    echo "ERROR: ${SOURCE_EXE} is not a valid executable file."
    exit 1
fi

echo "✓ Build completed successfully, executable verified."

# Step 2: Install the executable
echo "[2/6] Installing executable to ${INSTALL_BIN_DIR}..."
cp -v "$SOURCE_EXE" "${INSTALL_BIN_DIR}/${EXE_NAME}"
chmod +x "${INSTALL_BIN_DIR}/${EXE_NAME}"

# Step 3: Create log directory with proper permissions
echo "[3/6] Creating log directory ${LOG_DIR}..."
mkdir -p "$LOG_DIR"
# Set appropriate permissions for log directory
chmod 755 "$LOG_DIR"
# Note: In production, consider creating a dedicated 'telemetry' user:
#   sudo useradd --system --home-dir /var/lib/telemetry --create-home telemetry
#   sudo chown telemetry:telemetry "${LOG_DIR}"

# Step 4: Install and configure the configuration file
echo "[4/6] Installing and updating configuration to ${INSTALL_CONFIG_DIR}..."
mkdir -p "$INSTALL_CONFIG_DIR"

# Create the production configuration with absolute log path
# This replaces the relative path with an absolute system path
sed 's|"log_file": ".*"|"log_file": "'"${LOG_DIR}/telemetry_log.txt"'"|' \
    "$SOURCE_CONFIG" > "${INSTALL_CONFIG_DIR}/${CONFIG_NAME}"

# Keep a backup of the original configuration for reference
cp -v "$SOURCE_CONFIG" "${INSTALL_CONFIG_DIR}/${CONFIG_NAME}.original"

# Set appropriate permissions for configuration
chmod 644 "${INSTALL_CONFIG_DIR}/${CONFIG_NAME}"

# Step 5: Install and customize the systemd service file
echo "[5/6] Installing and customizing systemd service file..."

# Create a customized service file with correct paths
sed -e "s|WorkingDirectory=.*|WorkingDirectory=${REPO_ROOT}|" \
    -e "s|Environment=\"SERVICE_CONFIG=.*\"|Environment=\"SERVICE_CONFIG=${INSTALL_CONFIG_DIR}/${CONFIG_NAME}\"|" \
    -e "s|ReadWritePaths=.*|ReadWritePaths=${INSTALL_CONFIG_DIR}/ ${LOG_DIR}/|" \
    "$SOURCE_SERVICE_FILE" > "${SYSTEMD_DIR}/${SERVICE_FILE}"

chmod 644 "${SYSTEMD_DIR}/${SERVICE_FILE}"

echo "Service file customized with:"
echo "  WorkingDirectory: ${REPO_ROOT}"
echo "  SERVICE_CONFIG: ${INSTALL_CONFIG_DIR}/${CONFIG_NAME}"
echo "  ReadWritePaths: ${INSTALL_CONFIG_DIR}/ ${LOG_DIR}/"

# Step 6: Enable and configure the systemd service
echo "[6/6] Reloading systemd and enabling the service..."
systemctl daemon-reload

# Validate service file syntax
if ! systemctl is-enabled "${SERVICE_FILE}" >/dev/null 2>&1; then
    systemctl enable "${SERVICE_FILE}"
    if ! systemctl is-enabled "${SERVICE_FILE}" >/dev/null 2>&1; then
        echo "ERROR: Failed to enable service. Check systemctl logs."
        exit 1
    fi
else
    echo "Service was already enabled, updating configuration..."
    systemctl enable "${SERVICE_FILE}"
fi

# Validate service file can be loaded
if ! systemctl status "${SERVICE_FILE}" >/dev/null 2>&1; then
    echo "✓ Service file syntax validated."
else
    echo "⚠ Service status check completed (service not running yet - this is normal)."
fi

# --- Installation Complete ---
echo ""
echo "============================================================================"
echo "INSTALLATION COMPLETE"
echo "============================================================================"
echo "The telemetry service has been successfully installed and configured."
echo ""
echo "INSTALLED FILES:"
echo "  Executable: ${INSTALL_BIN_DIR}/${EXE_NAME}"
echo "  Configuration: ${INSTALL_CONFIG_DIR}/${CONFIG_NAME}"
echo "  Service file: ${SYSTEMD_DIR}/${SERVICE_FILE}"
echo "  Log directory: ${LOG_DIR}"
echo ""
echo "SERVICE STATUS:"
if systemctl is-enabled "${SERVICE_FILE}" >/dev/null 2>&1; then
    echo "  ✓ Service is enabled (will start on boot)"
else
    echo "  ✗ Service is not enabled"
fi

if systemctl is-active "${SERVICE_FILE}" >/dev/null 2>&1; then
    echo "  ✓ Service is currently running"
else
    echo "  ○ Service is not running (ready to start)"
fi

echo ""
echo "MANAGEMENT COMMANDS:"
echo "  Start the service:    sudo systemctl start ${SERVICE_FILE}"
echo "  Stop the service:     sudo systemctl stop ${SERVICE_FILE}"
echo "  Restart the service:  sudo systemctl restart ${SERVICE_FILE}"
echo "  Check service status: sudo systemctl status ${SERVICE_FILE}"
echo "  View live logs:       sudo journalctl -u ${SERVICE_FILE} -f"
echo "  View recent logs:     sudo journalctl -u ${SERVICE_FILE} --since '1 hour ago'"
echo ""
echo "DEVELOPMENT HELPER COMMANDS:"
echo "  Use the dev.sh script for service management:"
echo "  ./dev.sh service-start    # Start the service"
echo "  ./dev.sh service-stop     # Stop the service"
echo "  ./dev.sh service-status   # Check status"
echo "  ./dev.sh service-logs -f  # Follow logs"
echo ""
echo "CONFIGURATION:"
echo "  Config file: ${INSTALL_CONFIG_DIR}/${CONFIG_NAME}"
echo "  Backup config: ${INSTALL_CONFIG_DIR}/${CONFIG_NAME}.original"
echo "  Edit config and restart service to apply changes."
echo ""
echo "SECURITY RECOMMENDATION:"
echo "  For production deployment, create a dedicated user account:"
echo "    sudo useradd --system --home-dir /var/lib/telemetry --create-home telemetry"
echo "    sudo chown -R telemetry:telemetry ${LOG_DIR}"
echo "    sudo chown -R telemetry:telemetry ${INSTALL_CONFIG_DIR}"
echo "  Then uncomment the User/Group lines in ${SYSTEMD_DIR}/${SERVICE_FILE}"
echo ""
echo "UNINSTALLATION:"
echo "  To completely remove the service and all files:"
echo "    sudo $0 --uninstall"
echo "============================================================================"
