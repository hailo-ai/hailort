#!/bin/bash
set -e

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)" >&2
   exit 1
fi

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE_DIR_DEST="/lib/firmware/hailo/hailo10h_usb/"
readonly UPLOAD_TOOL_DEST="/usr/bin/hailo_usb_loader"

readonly FUNCTIONAL_RFS_FILE="${SCRIPT_DIR}/core-image-hailo-hailo10-usb-dongle.ext4"
readonly SWU_RFS_FILE="${SCRIPT_DIR}/core-image-hailo-hailo10-usb-dongle-swu.ext4"
readonly FW_UPDATE_IMAGE_FILE="${SCRIPT_DIR}/hailo-update-image-hailo10-usb-dongle.swu"
readonly UPLOAD_TOOL="${SCRIPT_DIR}/hailo_usb_loader"
readonly LOAD_IMAGE_SCRIPT="${SCRIPT_DIR}/load_image.sh"
readonly LOAD_IMAGE_LOCK_DIR="/run/lock/hailo"

readonly UDEV_RULES_FILE="${SCRIPT_DIR}/51-hailo-usb-udev.rules"
readonly UDEV_RULES_DEST="/etc/udev/rules.d/"

VERIFY_DEVICE=true
for arg in "$@"; do
    if [[ "$arg" == "--no-verify" ]]; then
        VERIFY_DEVICE=false
    fi
done

echo "Creating directories..."
mkdir -p -m 777 "${IMAGE_DIR_DEST}" || { echo "Error: Failed to create ${IMAGE_DIR_DEST} (permission denied?)" >&2; exit 1; }
chmod -R 777 "$(dirname "$(dirname "${IMAGE_DIR_DEST}")")" 2>/dev/null || chmod -R 777 "${IMAGE_DIR_DEST}"

echo "Verifying files..."
files_to_copy=(
    "${FUNCTIONAL_RFS_FILE}"
    "${SWU_RFS_FILE}"
    "${FW_UPDATE_IMAGE_FILE}"
    "${UPLOAD_TOOL}"
    "${UDEV_RULES_FILE}"
    "${LOAD_IMAGE_SCRIPT}"
)

for f in "${files_to_copy[@]}"; do
    if [[ ! -f "$f" ]]; then
        echo "Error: Required file missing: $f" >&2
        exit 1
    fi
done

echo "Installing hailo_usb_loader..."
cp "${UPLOAD_TOOL}" "${UPLOAD_TOOL_DEST}"
chmod +x "${UPLOAD_TOOL_DEST}"

echo "Copying image files (.ext4 and .swu)..."
image_files=("${FUNCTIONAL_RFS_FILE}" "${SWU_RFS_FILE}" "${FW_UPDATE_IMAGE_FILE}" "${LOAD_IMAGE_SCRIPT}")
chmod +x "${LOAD_IMAGE_SCRIPT}"
cp "${image_files[@]}" "${IMAGE_DIR_DEST}" || { echo "Error: Failed to copy image files" >&2; exit 1; }

mkdir -p -m 777 "${LOAD_IMAGE_LOCK_DIR}" || true

echo "Installing udev rules..."
cp "${UDEV_RULES_FILE}" "${UDEV_RULES_DEST}"

echo "Reloading udev rules and triggering..."
udevadm control --reload-rules
udevadm trigger --subsystem-match=usb --action=add

if [[ "${VERIFY_DEVICE}" == "true" ]]; then
    # Product name must match USB_PRODUCT in hailort/hailort_server/usb/hailort_usb_setup.sh
    readonly USB_PRODUCT_NAME="UGen300 USB AI Accelerator (Hailo-10H)"
    readonly USB_DEVICE_STRING="0b05:1d6f ASUSTek Computer, Inc. ${USB_PRODUCT_NAME}"
    readonly MAX_RETRIES=10
    readonly RETRY_INTERVAL=2

    echo "Waiting for ${USB_PRODUCT_NAME} device to appear..."

    for ((i=1; i<=MAX_RETRIES; i++)); do
        if lsusb | grep -q "${USB_DEVICE_STRING}"; then
            echo -e "\n${USB_PRODUCT_NAME} device found. USB configured successfully."
            exit 0
        fi
        echo -n "."
        sleep $RETRY_INTERVAL
    done

    echo -e "\n${USB_PRODUCT_NAME} device (${USB_DEVICE_STRING}) not found after $MAX_RETRIES retries"
    exit 1
else
    echo "Skipping USB device verification (--no-verify flag provided)"
fi
