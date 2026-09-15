#!/bin/bash
set -e

readonly UPLOAD_BIN="/usr/bin/hailo_usb_loader"
readonly UPLOAD_MODE="rfs-upload"
readonly LOCK_DIR="/run/lock/hailo"

if [[ ! -x "${UPLOAD_BIN}" ]]; then
    logger -t hailo-usb -p user.err "hailo_usb_loader not found or not executable: ${UPLOAD_BIN}"
    exit 1
fi

mkdir -p -m 777 "${LOCK_DIR}" || true

logger -t hailo-usb -p user.info "Starting image upload in mode: ${UPLOAD_MODE}"
if ! "${UPLOAD_BIN}" "${UPLOAD_MODE}"; then
    logger -t hailo-usb -p user.err "Failed to upload image in mode: ${UPLOAD_MODE}"
    exit 1
fi