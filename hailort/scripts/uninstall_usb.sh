#!/bin/bash
set -e

if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)." >&2
   exit 1
fi

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE_DIR_DEST="/lib/firmware/hailo/hailo10h_usb/"
readonly UPLOAD_TOOL_DEST="/usr/bin/hailo_usb_loader"
readonly UDEV_RULES_FILENAME="51-hailo-usb-udev.rules"
readonly OVERRIDING_UDEV_RULES_FILE="/etc/udev/rules.d/${UDEV_RULES_FILENAME}"
readonly USB_UDEV_RULES_FILE="/lib/udev/rules.d/${UDEV_RULES_FILENAME}"
readonly LOAD_IMAGE_LOCK_DIR="/run/lock/hailo"

echo "Starting uninstallation and cleanup..."

echo "Removing udev rules..."
has_removed_rules=false
if [[ -f "${OVERRIDING_UDEV_RULES_FILE}" || -L "${OVERRIDING_UDEV_RULES_FILE}" ]]; then
    rm -f "${OVERRIDING_UDEV_RULES_FILE}"
    echo "Udev rules ${OVERRIDING_UDEV_RULES_FILE} removed."
    has_removed_rules=true
fi
if [[ -f "${USB_UDEV_RULES_FILE}" ]]; then
    rm -f "${USB_UDEV_RULES_FILE}"
    echo "Udev rules ${USB_UDEV_RULES_FILE} removed."
    has_removed_rules=true
fi

if [[ "${has_removed_rules}" == "true" ]]; then
    echo "Reloading udev rules..."
    udevadm control --reload-rules
    udevadm trigger --subsystem-match=usb --action=add
else
    echo "No udev rules removed, skipping udev reload."
fi

echo "Removing upload tool..."
if [[ -f "${UPLOAD_TOOL_DEST}" ]]; then
    rm -f "${UPLOAD_TOOL_DEST}"
    echo "Tool ${UPLOAD_TOOL_DEST} removed."
else
    echo "Upload tool not found, skipping."
fi

echo "Removing image files and load_usb_image.sh script..."
if [[ -d "${IMAGE_DIR_DEST}" ]]; then
    rm -rf "${IMAGE_DIR_DEST:?}"/*
    echo "Directory ${IMAGE_DIR_DEST} cleaned."
else
    echo "Directory not found, skipping."
fi

echo "Removing empty directories..."
rmdir /lib/firmware/hailo/hailo10h_usb 2>/dev/null && echo "Removed /lib/firmware/hailo/hailo10h_usb" || echo "/lib/firmware/hailo/hailo10h_usb not empty or already removed"

rm -r "${LOAD_IMAGE_LOCK_DIR}" || true

echo "USB uninstallation complete."
