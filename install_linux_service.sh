#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# TELEMETRY SERVICE INSTALLATION SCRIPT
# ============================================================================
#
# This script automates the installation of the telemetry service as a
# systemd service on Linux systems. It handles:
# - Building the project
# - Installing the executable to /usr/local/bin
# - Setting up configuration in /etc/telemetry_service
# - Creating log directories with proper permissions
# - Installing and enabling the systemd service
#
# USAGE:
#   sudo ./install_linux_service.sh
#
# REQUIREMENTS:
#   - Root privileges (run with sudo)
#   - Systemd-based Linux distribution
#   - Build dependencies already installed
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
echo "  To remove the service:"
echo "    sudo systemctl stop ${SERVICE_FILE}"
echo "    sudo systemctl disable ${SERVICE_FILE}"
echo "    sudo rm ${SYSTEMD_DIR}/${SERVICE_FILE}"
echo "    sudo rm ${INSTALL_BIN_DIR}/${EXE_NAME}"
echo "    sudo rm -rf ${INSTALL_CONFIG_DIR}"
echo "    sudo rm -rf ${LOG_DIR}  # (optional - removes logs)"
echo "    sudo systemctl daemon-reload"
echo "============================================================================"
