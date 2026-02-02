#!/bin/bash
set -e

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE_FILE="/opt/hailo/image_files/core-image-hailo-hailo10-usb-dongle-ces.ext4"
readonly LINK_FILE="/opt/hailo/image_files/current_image.ext4"

echo "Creating directories..."
mkdir -p /opt/hailo/bin/ || { echo "Error: Failed to create /opt/hailo/bin/ (permission denied?)" >&2; exit 1; }
mkdir -p /opt/hailo/image_files/ || { echo "Error: Failed to create /opt/hailo/image_files/ (permission denied?)" >&2; exit 1; }
mkdir -p /opt/hailo/scripts/ || { echo "Error: Failed to create /opt/hailo/scripts/ (permission denied?)" >&2; exit 1; }

echo "Copying load_image.sh..."
if [[ ! -f "${SCRIPT_DIR}/load_image.sh" ]]; then
    echo "Error: load_image.sh not found in ${SCRIPT_DIR}" >&2
    exit 1
fi
cp "${SCRIPT_DIR}/load_image.sh" /opt/hailo/scripts/ || { echo "Error: Failed to copy load_image.sh" >&2; exit 1; }
chmod +x /opt/hailo/scripts/load_image.sh

echo "Copying hailo_rfs_upload..."
if [[ ! -f "${SCRIPT_DIR}/hailo_rfs_upload" ]]; then
    echo "Error: hailo_rfs_upload not found in ${SCRIPT_DIR}" >&2
    exit 1
fi
cp "${SCRIPT_DIR}/hailo_rfs_upload" /opt/hailo/bin/ || { echo "Error: Failed to copy hailo_rfs_upload" >&2; exit 1; }
chmod +x /opt/hailo/bin/hailo_rfs_upload

echo "Copying ext4 image files..."
shopt -s nullglob
ext4_files=("${SCRIPT_DIR}"/*.ext4)
shopt -u nullglob
if [[ ${#ext4_files[@]} -eq 0 ]]; then
    echo "Error: No .ext4 files found in ${SCRIPT_DIR}" >&2
    exit 1
fi
cp "${ext4_files[@]}" /opt/hailo/image_files/ || { echo "Error: Failed to copy .ext4 files" >&2; exit 1; }

echo "Creating symlink to current image..."
if [[ ! -f "${IMAGE_FILE}" ]]; then
    echo "Error: ${IMAGE_FILE} not found" >&2
    exit 1
fi
ln -sf "${IMAGE_FILE}" "${LINK_FILE}" || { echo "Error: Failed to create symlink ${LINK_FILE}" >&2; exit 1; }

echo "Reloading udev rules and triggering..."
udevadm control --reload-rules
udevadm trigger --subsystem-match=usb --action=add

readonly USB_DEVICE_STRING="0b05:1d6f ASUSTek Computer, Inc. HailoRT USB FunctionFS"
readonly MAX_RETRIES=10
readonly RETRY_INTERVAL=2

echo "Waiting for HailoRT USB FunctionFS device to appear..."

for ((i=1; i<=MAX_RETRIES; i++)); do
    if lsusb | grep -q "${USB_DEVICE_STRING}"; then
        echo -e "\nHailoRT USB FunctionFS device found. USB configured successfully."
        exit 0
    fi
    echo -n "."
    sleep $RETRY_INTERVAL
done

echo -e "\nHailoRT USB FunctionFS device (${USB_DEVICE_STRING}) not found after $MAX_RETRIES retries"
exit 1