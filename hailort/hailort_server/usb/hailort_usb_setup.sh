#!/usr/bin/env bash
set -euo pipefail

# Configuration
readonly CONFIGFS="/sys/kernel/config"
readonly GADGET="${CONFIGFS}/usb_gadget/hailort_usb"
readonly CONFIG_NAME="c.1"

readonly FFS_FUNCTION="ffs.hailo"
readonly FFS_DEVICE_NAME="${FFS_FUNCTION#ffs.}"
readonly FFS_MOUNT="/dev/ffs-hailo"

# USB Device Descriptors
readonly USB_VID="0x0B05"
readonly USB_PID="0x1D6F"
readonly USB_BCD_DEVICE="0x0100"
readonly USB_BCD_USB="0x0320"
readonly USB_DEVICE_CLASS="0xFF"
readonly USB_DEVICE_SUBCLASS="0x00"
readonly USB_DEVICE_PROTOCOL="0x00"
readonly USB_MAX_PACKET_SIZE="64"

readonly USB_LANGID="0x0409"

# USB Device Strings
readonly USB_MANUFACTURER="Hailo Technologies Ltd."
readonly USB_PRODUCT="UGen300 USB AI Accelerator (Hailo-10H)"
readonly USB_SERIAL="HRT-USB-001"
readonly USB_CONFIGURATION="HailoRT USB"
readonly USB_MAX_POWER_MA="500"

# Timeouts and intervals
readonly DESCRIPTOR_WAIT_TIMEOUT=10
readonly DESCRIPTOR_WAIT_INTERVAL=0.5
readonly UDC_DISABLE_SLEEP=0.2
readonly UDC_ENABLE_SLEEP=0.2
readonly CLEANUP_UDC_DISABLE_SLEEP=0.5

# Utility Functions
die() {
    echo "ERROR: $*" >&2
    exit 1
}

log_info() {
    echo "$*"
}

log_warn() {
    echo "WARNING: $*" >&2
}

# UDC Management
is_udc_active() {
    [[ -f "${GADGET}/UDC" ]] || return 1
    local udc_content
    udc_content=$(cat "${GADGET}/UDC" 2>/dev/null || echo "")
    [[ -n "${udc_content}" ]]
}

disable_udc() {
    [[ -d "${GADGET}" ]] || return 0
    
    if ! is_udc_active; then
        return 0
    fi
    
    echo "" > "${GADGET}/UDC" 2>/dev/null || true
    sleep "${UDC_DISABLE_SLEEP}"
}

get_udc() {
    local udc
    udc=$(ls /sys/class/udc 2>/dev/null | head -n1)
    [[ -n "${udc}" ]] || die "No UDC found"
    echo "${udc}"
}

# ConfigFS Management

ensure_configfs_mounted() {
    if ! mountpoint -q "${CONFIGFS}"; then
        mount -t configfs none "${CONFIGFS}" || die "Failed to mount configfs"
    fi
}

# Gadget Descriptor Creation
create_gadget_descriptors() {
    cd "${GADGET}" || die "Failed to change to gadget directory"
    
    echo "${USB_VID}" > idVendor
    echo "${USB_PID}" > idProduct
    echo "${USB_BCD_DEVICE}" > bcdDevice
    echo "${USB_BCD_USB}" > bcdUSB
    echo "${USB_DEVICE_CLASS}" > bDeviceClass
    echo "${USB_DEVICE_SUBCLASS}" > bDeviceSubClass
    echo "${USB_DEVICE_PROTOCOL}" > bDeviceProtocol
    echo "${USB_MAX_PACKET_SIZE}" > bMaxPacketSize0
}

create_gadget_strings() {
    cd "${GADGET}" || die "Failed to change to gadget directory"
    
    mkdir -p "strings/${USB_LANGID}"
    echo "${USB_MANUFACTURER}" > "strings/${USB_LANGID}/manufacturer"
    echo "${USB_PRODUCT}" > "strings/${USB_LANGID}/product"
    echo "${USB_SERIAL}" > "strings/${USB_LANGID}/serialnumber"
}

create_gadget_config() {
    cd "${GADGET}" || die "Failed to change to gadget directory"
    
    mkdir -p "configs/${CONFIG_NAME}/strings/${USB_LANGID}"
    echo "${USB_CONFIGURATION}" > "configs/${CONFIG_NAME}/strings/${USB_LANGID}/configuration"
    echo "${USB_MAX_POWER_MA}" > "configs/${CONFIG_NAME}/MaxPower"
}

# FunctionFS Management
create_functionfs_function() {
    cd "${GADGET}" || die "Failed to change to gadget directory"
    
    if ! mkdir -p "functions/${FFS_FUNCTION}" 2>/dev/null; then
        if [[ ! -d "functions/${FFS_FUNCTION}" ]]; then
            die "Failed to create functionfs function. Check dmesg for details. Make sure UDC is disabled."
        fi
    fi
    
    [[ -d "functions/${FFS_FUNCTION}" ]] || die "Functionfs function does not exist"
    
    rm -f "configs/${CONFIG_NAME}/${FFS_FUNCTION}" 2>/dev/null || true
    ln -sf "functions/${FFS_FUNCTION}" "configs/${CONFIG_NAME}/${FFS_FUNCTION}"
}

mount_functionfs() {
    mkdir -p "${FFS_MOUNT}"
    
    if mountpoint -q "${FFS_MOUNT}"; then
        if ! grep -q "functionfs.*hailo" /proc/mounts 2>/dev/null; then
            die "FunctionFS mount point exists but not for hailo"
        fi
        return 0
    fi
    
    mount -t functionfs "${FFS_DEVICE_NAME}" "${FFS_MOUNT}" || \
        die "Failed to mount FunctionFS. Check dmesg for details."
}

unmount_functionfs() {
    if ! mountpoint -q "${FFS_MOUNT}" 2>/dev/null; then
        return 0
    fi
    
    log_info "Unmounting ${FFS_MOUNT}..."
    umount -f "${FFS_MOUNT}" 2>/dev/null || umount -l "${FFS_MOUNT}" 2>/dev/null || true
    rmdir "${FFS_MOUNT}" 2>/dev/null || true
}

# Descriptor Waiting
wait_for_descriptors() {
    local elapsed=0
    local timeout="${DESCRIPTOR_WAIT_TIMEOUT}"
    local interval="${DESCRIPTOR_WAIT_INTERVAL}"
    
    while (( elapsed < timeout )); do
        if [[ -e "${FFS_MOUNT}/ep1" ]] || [[ -e "${FFS_MOUNT}/ep2" ]]; then
            log_info "FFS Descriptors written, endpoints created"
            return 0
        fi
        
        sleep "${interval}"
        elapsed=$((elapsed + 1))
    done
    
    die "Timeout waiting for descriptors. Make sure the daemon is running and has written descriptors to EP0."
}

setup() {
    ensure_configfs_mounted
    
    if [[ -d "${GADGET}" ]] && [[ -f "${GADGET}/idVendor" ]]; then
        if [[ -d "${GADGET}/functions/${FFS_FUNCTION}" ]]; then
            if ! mountpoint -q "${FFS_MOUNT}"; then
                mount_functionfs
            fi
        fi
        return 0
    fi
    
    disable_udc
    
    mkdir -p "${GADGET}"
    create_gadget_descriptors
    create_gadget_strings
    create_gadget_config
    create_functionfs_function
    mount_functionfs
}

enable() {
    local udc
    udc=$(get_udc)
    
    [[ -d "${GADGET}" ]] || die "Gadget not configured. Run 'setup' first."
    [[ -e "${FFS_MOUNT}/ep0" ]] || die "FunctionFS ep0 not available at ${FFS_MOUNT}. Start daemon first."
    
    wait_for_descriptors
    
    if is_udc_active; then
        local active_udc
        active_udc=$(cat "${GADGET}/UDC" 2>/dev/null || echo "")
        log_info "UDC already active: ${active_udc}"
        return 0
    fi
    
    # Disable legacy gadget right before enabling FFS UDC to minimize
    # the disconnect-to-reconnect window and prevent host autosuspend race
    if [[ -e /sys/kernel/hailo_gadget/hailo_gadget ]]; then
        echo "disable" > /sys/kernel/hailo_gadget/hailo_gadget
    fi

    if ! echo "${udc}" > "${GADGET}/UDC" 2>/dev/null; then
        die "Failed to enable UDC. Check dmesg for details."
    fi

    sleep "${UDC_ENABLE_SLEEP}"
}

disable() {
    [[ -d "${GADGET}" ]] || die "Gadget not configured."
    
    if ! is_udc_active; then
        log_info "UDC not active"
        return 0
    fi
    
    if ! echo "" > "${GADGET}/UDC" 2>/dev/null; then
        die "Failed to disable UDC"
    fi
}

# Cleanup Operations
remove_functionfs_function() {
    cd "${GADGET}" 2>/dev/null || return 0
    
    if [[ -d "configs/${CONFIG_NAME}" ]]; then
        rm -f "configs/${CONFIG_NAME}/${FFS_FUNCTION}" 2>/dev/null || true
    fi
    
    if [[ -d "functions/${FFS_FUNCTION}" ]]; then
        log_info "Removing function ${FFS_FUNCTION}..."
        rmdir "functions/${FFS_FUNCTION}" 2>/dev/null || true
    fi
}

remove_gadget_config() {
    cd "${GADGET}" 2>/dev/null || return 0
    
    if [[ -d "configs/${CONFIG_NAME}" ]]; then
        if [[ -d "configs/${CONFIG_NAME}/strings/${USB_LANGID}" ]]; then
            rmdir "configs/${CONFIG_NAME}/strings/${USB_LANGID}" 2>/dev/null || true
        fi
        if [[ -d "configs/${CONFIG_NAME}/strings" ]]; then
            rmdir "configs/${CONFIG_NAME}/strings" 2>/dev/null || true
        fi
        rmdir "configs/${CONFIG_NAME}" 2>/dev/null || true
    fi
    rmdir "configs" 2>/dev/null || true
}

remove_gadget() {
    cd / 2>/dev/null || true
    
    if [[ -d "${GADGET}/strings/${USB_LANGID}" ]]; then
        rmdir "${GADGET}/strings/${USB_LANGID}" 2>/dev/null || true
    fi
    if [[ -d "${GADGET}/strings" ]]; then
        rmdir "${GADGET}/strings" 2>/dev/null || true
    fi
    
    if [[ -d "${GADGET}" ]]; then
        rmdir "${GADGET}" 2>/dev/null || true
    fi
}

cleanup() {
    log_info "Cleaning up USB Gadget..."
    
    [[ -d "${GADGET}" ]] || {
        log_info "Gadget not configured, nothing to clean up"
        return 0
    }
    
    if is_udc_active; then
        log_info "Disabling UDC..."
        disable_udc
    fi
    
    unmount_functionfs
    
    if ! is_udc_active; then
        remove_functionfs_function
        remove_gadget_config
        remove_gadget
    else
        log_warn "UDC is still active. Some resources may not be cleaned up."
        log_warn "Run 'disable' first, then 'cleanup' again."
    fi
    
    log_info "Cleanup done."
}

[[ "${EUID}" -eq 0 ]] || die "Must run as root"

case "${1:-}" in
    setup) setup ;;
    enable) enable ;;
    disable) disable ;;
    cleanup) cleanup ;;
    *) 
        echo "Usage: $0 {setup|enable|disable|cleanup}"
        exit 1
        ;;
esac
