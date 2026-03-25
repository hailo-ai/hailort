#!/bin/bash
set -e

if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)." >&2
   exit 1
fi

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE_DIR_DEST="/lib/firmware/hailo/hailo10h_usb/"
readonly UPLOAD_TOOL_DEST="/usr/bin/hailo_usb_loader"
readonly USB_UDEV_RULES="/etc/udev/rules.d/51-hailo-usb-udev.rules"
readonly LOAD_IMAGE_LOCK_DIR="/run/lock/hailo"

echo "Starting uninstallation and cleanup..."

echo "Removing udev rules..."
if [[ -f "${USB_UDEV_RULES}" ]]; then
    rm -f "${USB_UDEV_RULES}"
    echo "Reloading udev rules..."
    udevadm control --reload-rules
    udevadm trigger --subsystem-match=usb --action=add
    echo "Udev rules removed."
else
    echo "Udev rules not found, skipping."
fi

echo "Removing upload tool..."
if [[ -f "${UPLOAD_TOOL_DEST}" ]]; then
    rm -f "${UPLOAD_TOOL_DEST}"
    echo "Tool ${UPLOAD_TOOL_DEST} removed."
else
    echo "Upload tool not found, skipping."
fi

echo "Removing image files and load_image.sh script..."
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
