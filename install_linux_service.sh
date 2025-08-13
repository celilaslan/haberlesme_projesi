#!/usr/bin/env bash
set -euo pipefail

# Installation script for the telemetry_service systemd service.
# This script should be run with sudo.

# --- Configuration ---
# Source directory of the project
REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# Destination for the executable
INSTALL_BIN_DIR="/usr/local/bin"
EXE_NAME="telemetry_service"
SOURCE_EXE="${REPO_ROOT}/telemetry_service/${EXE_NAME}"

# Destination for the configuration file
INSTALL_CONFIG_DIR="/etc/telemetry_service"
CONFIG_NAME="service_config.json"
SOURCE_CONFIG="${REPO_ROOT}/${CONFIG_NAME}"

# Destination for the systemd service file
SYSTEMD_DIR="/etc/systemd/system"
SERVICE_FILE="telemetry_service.service"
SOURCE_SERVICE_FILE="${REPO_ROOT}/telemetry_service/${SERVICE_FILE}"

# --- Script Logic ---

# Check for root privileges
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)." 
   exit 1
fi

echo "--- Telemetry Service Installer ---"

# 1. Build the project first
echo "[1/6] Building the project..."
if ! "${REPO_ROOT}/dev.sh" build; then
    echo "Error: Build failed. Please fix build errors before installing."
    exit 1
fi

if [[ ! -f "$SOURCE_EXE" ]]; then
    echo "Error: Executable not found at ${SOURCE_EXE} after build."
    exit 1
fi

# 2. Copy the executable
echo "[2/6] Installing executable to ${INSTALL_BIN_DIR}..."
cp -v "$SOURCE_EXE" "${INSTALL_BIN_DIR}/${EXE_NAME}"
chmod +x "${INSTALL_BIN_DIR}/${EXE_NAME}"

# 3. Create log directory
echo "[3/6] Creating log directory /var/log/telemetry_service..."
mkdir -p /var/log/telemetry_service
# In a production setup, you would `chown telemetry:telemetry /var/log/telemetry_service`
# if you were running the service as a dedicated user.

# 4. Copy and modify the configuration file
echo "[4/6] Installing and updating configuration to ${INSTALL_CONFIG_DIR}..."
mkdir -p "$INSTALL_CONFIG_DIR"
# Use sed to replace the default relative log file path with an absolute one
sed 's|"log_file": ".*"|"log_file": "/var/log/telemetry_service/telemetry_log.txt"|' \
    "$SOURCE_CONFIG" > "${INSTALL_CONFIG_DIR}/${CONFIG_NAME}"
cp -v "$SOURCE_CONFIG" "${INSTALL_CONFIG_DIR}/${CONFIG_NAME}.original" # keep a copy of original

# 5. Copy the systemd service file
echo "[5/6] Installing systemd service file..."
cp -v "$SOURCE_SERVICE_FILE" "${SYSTEMD_DIR}/${SERVICE_FILE}"

# 6. Reload systemd and enable the service
echo "[6/6] Reloading systemd and enabling the service..."
systemctl daemon-reload
systemctl enable ${SERVICE_FILE}

echo ""
echo "--- Installation Complete ---"
echo "The telemetry_service is now installed and enabled to start on boot."
echo ""
echo "To start the service now, run:"
echo "  sudo systemctl start ${SERVICE_FILE}"
echo ""
echo "To see the service status, run:"
echo "  sudo systemctl status ${SERVICE_FILE}"
echo ""
echo "To view live logs, run:"
echo "  sudo journalctl -u ${SERVICE_FILE} -f"
echo ""
echo "NOTE: The service is configured to run as root by default."
echo "For better security, create a dedicated 'telemetry' user and"
echo "uncomment the User/Group lines in ${SYSTEMD_DIR}/${SERVICE_FILE}."
