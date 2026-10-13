/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_usb_loader.c
 * @brief Uploads RFS (Root File System) and FWU (FW Update) images to Hailo-10H USB devices using libusb.
 **/

#include "usb_loader_protocol.h"

#include <libusb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#endif

#define USB_VENDOR_ID               (0x0B05)
#define USB_PRODUCT_ID              (0x1D6F)

#define USB_INTERFACE_CLASS         (0xFF)
#define USB_INTERFACE_PROTOCOL      (1)
#define USB_INTERFACE_NUM_ENDPOINTS (2)
#define USB_INTERFACE_EP0           (0)

#define USB_CONFIG_RFS_MODE         (1)
#define USB_CONFIG_FWU_MODE         (2)
#define USB_TYPE_VENDOR             (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE)

//Define from Functional mode
#define REQ_SYS_REBOOT_FUNCTIONAL_MODE   (0x03)

// Defines from usb gadget driver
#define REQ_GET_STATUS              (0x03)
#define REQ_GET_BUILD_INFO          (0x11)
#define REQ_UPLOAD_START            (0x12)
#define REQ_UPLOAD_FINISH           (0x13)
#define REQ_GET_SKU_ID              (0x14)
#define REQ_CTRL                    (0x15)
#define REQ_GET_PROTOCOL_VERSION    (0x16)
#define REQ_SYS_REBOOT_FW_MODE      (0x16)
#define CTRL_GET_STATUS             (0)
#define CTRL_GET_RX_COUNT           (1)
#define CTRL_CLR_RX_COUNT           (2)
#define CTRL_GET_FWU_STATUS         (3)
#define FWU_STATE_IDLE              (0)
#define FWU_STATE_IN_PROGRESS       (1)
#define FWU_STATE_OK                (2)
#define FWU_STATE_FAIL              (3)
#define EXPECTED_PROTOCOL_VERSION   (1)
#define INFO_BUFFER_SIZE            (64)
#define PING_SIZE                   (64)
#define PING_BYTE                   (0xAA)

#define TIMEOUT_USB_MS              (10000)
#define TIMEOUT_POLL_SEC            (60)
#define TIMEOUT_FWU_SEC             (600)
#define TIMEOUT_DEVICE_BOOT_SEC     (60)
#define POLL_INTERVAL_US            (100000)
#define POLL_EARLY_FAIL_THRESHOLD   (10)
#define CHUNK_SIZE                  (64 * 1024)
#define MAX_FILE_SIZE               (256 * 1024 * 1024) // max size is defined by Hailo Gadget
#define SLEEP_DEVICE_CLOSE_SEC      (1)
#define SLEEP_CONFIG_MODE_US        (100000)
#define SLEEP_BULK_DRAIN_US         (250000)
#define SLEEP_REBOOT_SEC            (10)
#define SLEEP_DEVICE_BOOT_SEC       (1)
#define SLEEP_RFS_SETTLE_SEC        (4)
#define SLEEP_RESET_REBOOT_SEC      (3)
#define SLEEP_FWU_RECOVER_SEC       (5)
#define SLEEP_CHANGE_MODE_SEC       (10)

#include "compat.h"

// Hardcoded paths
#define PATH_RFS_OPERATIONAL        RFS_SWU_DIR "core-image-hailo-hailo10-usb-dongle.ext4"
#define PATH_RFS_UPDATE             RFS_SWU_DIR "core-image-hailo-hailo10-usb-dongle-swu.ext4"
#define PATH_FWU_IMAGE              RFS_SWU_DIR "hailo-update-image-hailo10-usb-dongle.swu"
#define PATH_VERSION_FILE           RFS_SWU_DIR "version"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ARR_INDEX_VALID(id, arr) ((size_t)(id) < ARRAY_SIZE(arr))

// Hardcoded values - Version specific
static const char *SUPPORTED_SKU_IDS[] = {"NA", "7", "8"};
#define BUILD_INFO_PREFIX           "Hailo Linux build v"
#define EXPECTED_BUILD_INFO         BUILD_INFO_PREFIX HAILO_USB_TARGET_FW_VERSION
#define RFS_UPLOAD_DEVICE_TIMEOUT_MESSAGE "Timed out waiting for the Hailo USB device"

static const char *fwu_error_strings[] = {"Success", "General error", "Device disconnected", "FWU error",
    "Image not specified", "Image not found", "Public key missing", "Boot partition error"};

typedef struct {
    struct libusb_context *ctx;
    struct libusb_device_handle *handle;
    uint8_t ep_bulk_out;
    uint8_t ep_interrupt_in;
} device_t;

typedef struct {
    device_t *device;
    size_t expected;
} poll_ctx_t;

typedef enum {
    FW_UPDATE_PHASE_RESTART_DEVICE = 0,
    FW_UPDATE_PHASE_UPLOAD_UPDATE_IMAGE,
    FW_UPDATE_PHASE_SWITCH_TO_UPDATE_MODE,
    FW_UPDATE_PHASE_UPLOAD_FW_PACKAGE,
    FW_UPDATE_PHASE_INSTALL_FW,
    FW_UPDATE_PHASE_RESTART_WITH_NEW_FW,
    FW_UPDATE_PHASE_ENUM_COUNT
} fw_update_phase_t;

static const char *FW_UPDATE_PHASE_LABELS[] = {
    "Restarting the device",
    "Uploading the update image",
    "Switching to update mode",
    "Uploading the firmware package",
    "Installing the firmware",
    "Restarting with the new firmware",
};

// C99 compile-time check: a false condition gives a negative array size
#define COMPILE_TIME_ASSERT(condition, name) typedef char name[(condition) ? 1 : -1]
COMPILE_TIME_ASSERT(ARRAY_SIZE(FW_UPDATE_PHASE_LABELS) == FW_UPDATE_PHASE_ENUM_COUNT,
    fw_update_phase_labels_size_check);

static void report_fw_update_phase(fw_update_phase_t phase)
{
    printf("%s%d/%d: %s\n", LOADER_OUTPUT_PHASE_PREFIX, (int)phase + 1, FW_UPDATE_PHASE_ENUM_COUNT,
        FW_UPDATE_PHASE_LABELS[phase]);
    fflush(stdout);
}

static bool poll_until(bool (*fn)(void *), void *ctx)
{
    const uint64_t start = compat_monotonic_sec();
    while ((compat_monotonic_sec() - start) < TIMEOUT_POLL_SEC) {
        if (fn(ctx)) {
            return true;
        }
        usleep(POLL_INTERVAL_US);
    }
    return false;
}

static bool vendor_request(device_t *dev, uint8_t req, uint16_t val, uint16_t idx, uint8_t *data, uint16_t len,
    int32_t dir)
{
    uint8_t type = (dir == LIBUSB_ENDPOINT_IN) ? (USB_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
                                               : (USB_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT);
    int32_t ret = libusb_control_transfer(dev->handle, type, req, val, idx, data, len, TIMEOUT_USB_MS);
    if (ret < 0) {
        return false;
    }
    return true;
}

static bool bulk_write(device_t *dev, uint8_t *data, int32_t len)
{
    int32_t transferred = 0;
    int32_t ret = libusb_bulk_transfer(dev->handle, dev->ep_bulk_out, data, len, &transferred, TIMEOUT_USB_MS);
    return (ret == LIBUSB_SUCCESS) && (transferred == len);
}

static bool is_target_device(const struct libusb_device_descriptor *desc)
{
    return (desc->idVendor == USB_VENDOR_ID) && (desc->idProduct == USB_PRODUCT_ID);
}

static bool is_functional_rfs(libusb_device_handle *handle)
{
    libusb_device *usb_dev = libusb_get_device(handle);

    struct libusb_device_descriptor desc = {0};
    int32_t rc = libusb_get_device_descriptor(usb_dev, &desc);
    if (rc != LIBUSB_SUCCESS) {
        fprintf(stderr, "Failed to get device descriptor: %d\n", rc);
        return false;
    }

    if (desc.iProduct == 0) {
        return false;
    }

    char name[INFO_BUFFER_SIZE] = {0};
    rc = libusb_get_string_descriptor_ascii(handle, desc.iProduct, (unsigned char *)name, sizeof(name));
    if (rc < 0) {
        fprintf(stderr, "Failed to get product name: %d\n", rc);
        return false;
    }

    if (strcmp(name, HAILO_USB_FUNCTIONAL_PRODUCT_NAME) != 0) {
        return false;
    }

    return true;
}

static bool usb_reset_device(void)
{
    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) != LIBUSB_SUCCESS) {
        return false;
    }

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(ctx, &list);
    bool is_reset_ok = false;
    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc = {0};
        if (libusb_get_device_descriptor(list[i], &desc) != LIBUSB_SUCCESS) {
            continue;
        }
        if (!is_target_device(&desc)) {
            continue;
        }
        libusb_device_handle *handle = NULL;
        if (libusb_open(list[i], &handle) == LIBUSB_SUCCESS) {
            is_reset_ok = is_functional_rfs(handle) ? compat_reset_handle(handle) : true;
            libusb_close(handle);
        }
        break;
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return is_reset_ok;
}

static bool is_upload_interface(const struct libusb_interface_descriptor *intf)
{
    return (intf->bInterfaceClass == USB_INTERFACE_CLASS) && (intf->bInterfaceProtocol == USB_INTERFACE_PROTOCOL) &&
           (intf->bNumEndpoints == USB_INTERFACE_NUM_ENDPOINTS);
}

static void find_endpoints(device_t *dev, const struct libusb_interface_descriptor *intf)
{
    dev->ep_bulk_out = 0;
    dev->ep_interrupt_in = 0;
    for (uint8_t i = 0; i < intf->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *ep = &intf->endpoint[i];
        uint8_t dir = ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK;
        uint8_t type = ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
        if ((dir == LIBUSB_ENDPOINT_OUT) && (type == LIBUSB_TRANSFER_TYPE_BULK)) {
            dev->ep_bulk_out = ep->bEndpointAddress;
        } else if (type == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
            dev->ep_interrupt_in = ep->bEndpointAddress;
        }
    }
}

static bool claim_interface(device_t *dev, uint8_t num)
{
    int32_t ret = libusb_claim_interface(dev->handle, num);
    if (ret != LIBUSB_SUCCESS) {
        fprintf(stderr, "Failed to claim interface: %d\n", ret);
        return false;
    }
    return true;
}

static void release_interface(device_t *dev)
{
    if (dev->handle) {
        libusb_release_interface(dev->handle, USB_INTERFACE_EP0);
        // TODO: adding print here on fail - shows fails - determine why we are calling this function and failing sometimes
    }
}

static bool open_usb_device(device_t *dev, libusb_device *usb_dev) {
    bool success = false;

    if (libusb_open(usb_dev, &dev->handle) != LIBUSB_SUCCESS) {
        goto exit;
    }

    struct libusb_config_descriptor *cfg = NULL;
    if (libusb_get_active_config_descriptor(usb_dev, &cfg) != LIBUSB_SUCCESS) {
        goto exit;
    }

    for (uint8_t i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface_descriptor *intf = &cfg->interface[i].altsetting[0];
        if (is_upload_interface(intf)) {
            find_endpoints(dev, intf);
            if (dev->ep_bulk_out && dev->ep_interrupt_in && claim_interface(dev, intf->bInterfaceNumber)) {
                success = true;
                break;
            }
        }
    }
    libusb_free_config_descriptor(cfg);

exit:
    if ((!success) && (NULL != dev) && (NULL != dev->handle)) {
        libusb_close(dev->handle);
        dev->handle = NULL;
    }
    return success;
}

static void device_close(device_t *dev)
{
    if (dev->handle) {
        release_interface(dev);
        libusb_close(dev->handle);
        dev->handle = NULL;
    }
    if (dev->ctx) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
    }

    // Sleep after closing device to make sure device fully closed (otherwise weird behavior occurs)
    sleep(SLEEP_DEVICE_CLOSE_SEC);
}

static bool get_fw_version(device_t *dev, uint8_t req, const char *expected, const char *name, char* device_fw_version_out_buf)
{
    char buf[INFO_BUFFER_SIZE] = {0};
    if (!vendor_request(dev, req, 0, 0, (uint8_t *)buf, sizeof(buf), LIBUSB_ENDPOINT_IN)) {
        fprintf(stderr, "Failed to get %s\n", name);
        return false;
    }
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "Unsupported %s: %s (expected %s)\n", name, buf, expected);
        return false;
    }
    if (NULL != device_fw_version_out_buf) {
        strcpy(device_fw_version_out_buf, buf + (sizeof(BUILD_INFO_PREFIX) - 1));
    }
    return true;
}

static bool validate_sku_id(device_t *dev)
{
    char buf[INFO_BUFFER_SIZE] = {0};
    if (!vendor_request(dev, REQ_GET_SKU_ID, 0, 0, (uint8_t *)buf, sizeof(buf), LIBUSB_ENDPOINT_IN)) {
        fprintf(stderr, "Failed to get SKU ID\n");
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(SUPPORTED_SKU_IDS); i++) {
        if (strcmp(buf, SUPPORTED_SKU_IDS[i]) == 0)
            return true;
    }
    fprintf(stderr, "Unsupported SKU ID: %s\n", buf);
    return false;
}

static bool validate_protocol_and_sku(device_t *dev)
{
    uint32_t ver = 0;
    if (!vendor_request(dev, REQ_GET_PROTOCOL_VERSION, 0, 0, (uint8_t *)&ver, sizeof(ver), LIBUSB_ENDPOINT_IN)) {
        fprintf(stderr, "Failed to get protocol version\n");
        return false;
    }
    if (le32toh(ver) != EXPECTED_PROTOCOL_VERSION) {
        fprintf(stderr, "Unsupported protocol version: %d\n", le32toh(ver));
        return false;
    }
    return validate_sku_id(dev);
}

static bool write_version_file(const char *device_fw_version)
{
    FILE *fp = compat_create_file_with_permissions(PATH_VERSION_FILE);
    if (NULL == fp) {
        fprintf(stderr, "Failed to open version file %s for writing\n", PATH_VERSION_FILE);
        return false;
    }
    fprintf(fp, "%s", device_fw_version);

    if (fclose(fp) != 0) {
        fprintf(stderr, "Failed to close version file %s\n", PATH_VERSION_FILE);
        return false;
    }
    return true;
}

static int32_t get_rx_count(device_t *dev)
{
    uint8_t buf[4] = {0};
    if (!vendor_request(dev, REQ_CTRL, CTRL_GET_RX_COUNT, 0, buf, sizeof(buf), LIBUSB_ENDPOINT_IN)) {
        return -1;
    }
    return (int32_t)le32toh(*(uint32_t *)buf);
}

static bool try_clear_rx_count(void *ctx)
{
    uint8_t resp = 0;
    return vendor_request((device_t *)ctx, REQ_CTRL, CTRL_CLR_RX_COUNT, 0, &resp, 1, LIBUSB_ENDPOINT_IN) && (resp == 1);
}

static bool clear_rx_count(device_t *dev)
{
    if (!poll_until(try_clear_rx_count, dev)) {
        fprintf(stderr, "Failed to clear RX counter\n");
        return false;
    }
    return true;
}

static bool try_ping(void *ctx)
{
    device_t *dev = (device_t *)ctx;
    uint8_t data[PING_SIZE];
    memset(data, PING_BYTE, sizeof(data));

    int32_t transferred = 0;
    int32_t ret = libusb_bulk_transfer(dev->handle, dev->ep_bulk_out, data, sizeof(data), &transferred, TIMEOUT_USB_MS);
    if (ret != LIBUSB_SUCCESS || transferred != PING_SIZE) {
        return false;
    }
    return (get_rx_count(dev) >= PING_SIZE) && clear_rx_count(dev);
}

static bool try_gadget_ready_to_upload(void *ctx)
{
    uint8_t status = 0;
    if (!vendor_request((device_t *)ctx, REQ_CTRL, CTRL_GET_STATUS, 0, &status, 1, LIBUSB_ENDPOINT_IN)) {
        return false;
    }
    return status != 0;
}

static bool wait_gadget_ready_to_upload(device_t *dev)
{
    // checks Hailo Gadget finished allocating a buffer. Seems redundant
    if (!poll_until(try_gadget_ready_to_upload, dev)) {
        fprintf(stderr, "Timeout waiting for gadget\n");
        return false;
    }
    // checks Hailo Gadget manages to read a small buffer
    if (!poll_until(try_ping, dev)) {
        fprintf(stderr, "Warning: ping failed\n");
    }
    return true;
}

static bool get_fwu_status(device_t *dev, uint32_t *state, uint32_t *exit_code)
{
    uint8_t buf[8] = {0};
    if (!vendor_request(dev, REQ_CTRL, CTRL_GET_FWU_STATUS, 0, buf, sizeof(buf), LIBUSB_ENDPOINT_IN)) {
        return false;
    }
    *state = le32toh(*(uint32_t *)buf);
    *exit_code = le32toh(*(uint32_t *)(buf + 4));
    return true;
}

static const char *fwu_error_string(uint32_t code)
{
    return (ARR_INDEX_VALID(code, fwu_error_strings)) ? fwu_error_strings[code] : "Unknown error";
}

static bool wait_fwu_complete(device_t *dev)
{
    const uint64_t start = compat_monotonic_sec();
    bool in_progress = false;

    for (int32_t i = 0; (compat_monotonic_sec() - start) < TIMEOUT_FWU_SEC; i++) {
        uint32_t state, code;
        if (get_fwu_status(dev, &state, &code)) {
            if (state == FWU_STATE_OK) {
                return true;
            }
            if (state == FWU_STATE_IN_PROGRESS) {
                in_progress = true;
            }
            // TODO: explain
            if (state == FWU_STATE_FAIL && (in_progress || i > POLL_EARLY_FAIL_THRESHOLD)) {
                fprintf(stderr, "FWU failed: %s\n", fwu_error_string(code));
                return false;
            }
        }
        usleep(POLL_INTERVAL_US);
    }
    fprintf(stderr, "FWU timeout\n");
    return false;
}

static FILE *file_open(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (long)(*size = (size_t)ftell(f)) <= 0 || *size > MAX_FILE_SIZE ||
        fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Invalid file: %s\n", path);
        fclose(f);
        return NULL;
    }
    return f;
}

static bool upload_data(device_t *dev, FILE *f)
{
    uint8_t buf[CHUNK_SIZE];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (!bulk_write(dev, buf, (int32_t)n)) {
            return false;
        }
    }
    return !ferror(f);
}

static bool send_upload_start(device_t *dev, size_t size)
{
    uint16_t lo = (uint16_t)(size & 0xFFFF);
    uint16_t hi = (uint16_t)((size >> 16) & 0xFFFF);
    if (!vendor_request(dev, REQ_UPLOAD_START, lo, hi, NULL, 0, LIBUSB_ENDPOINT_OUT)) {
        fprintf(stderr, "Upload start failed\n");
        return false;
    }
    return true;
}

// Send a zero-length packet to force USB controller to flush any batched data
static void send_zero_length_packet(device_t *dev) {
    unsigned char zlp_buffer[1] = {0};
    int transferred = 0;
    libusb_bulk_transfer(dev->handle, dev->ep_bulk_out, zlp_buffer, 0, &transferred, TIMEOUT_USB_MS);
}

static bool upload_image(device_t *dev, const char *path)
{
    size_t size = 0;
    FILE *image = file_open(path, &size);
    if (!image) {
        return false;
    }

    bool success = false;
    if (!claim_interface(dev, USB_INTERFACE_EP0)) {
        fprintf(stderr, "Error claiming EP 0 after switching to fwu mode\n");
        goto exit;
    }

    if (!send_upload_start(dev, size) || !wait_gadget_ready_to_upload(dev)) {
        fprintf(stderr, "Server isn't ready\n");
        goto exit;
    }

    if (!upload_data(dev, image)) {
        fprintf(stderr, "Data transfer failed\n");
        goto exit;
    }

    send_zero_length_packet(dev);

    usleep(SLEEP_BULK_DRAIN_US); /* Wait for bulk transfer pipeline to drain */

    success = true;
exit:
    fclose(image);
    return success;
}

static bool reboot(device_t *dev)
{
    if (!vendor_request(dev, REQ_SYS_REBOOT_FW_MODE, 0, 0, NULL, 0, LIBUSB_ENDPOINT_OUT)) {
        fprintf(stderr, "Reboot failed\n");
        return false;
    }

    // sleep to make sure that the reboot has taken affect and we dont get old device
    sleep(SLEEP_REBOOT_SEC);
    return true;
}

static bool find_and_open_target_device(device_t *dev)
{
    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(dev->ctx, &list);
    if (count < 0) {
        return false;
    }

    bool opened = false;
    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc = {0};

        if (libusb_get_device_descriptor(list[i], &desc) != LIBUSB_SUCCESS) {
            fprintf(stderr, "Error getting device descriptor\n");
            continue;
        }

        if (is_target_device(&desc)) {
            if (open_usb_device(dev, list[i])) {
                opened = true;
                break;
            }
        }
    }

    libusb_free_device_list(list, 1);
    return opened;
}

static bool wait_for_device_boot(device_t *dev, const char *timeout_message)
{
    if (libusb_init(&dev->ctx) != LIBUSB_SUCCESS) {
        fprintf(stderr, "Failed to initialize libusb\n");
        return false;
    }

    const uint64_t start = compat_monotonic_sec();
    while ((compat_monotonic_sec() - start) < TIMEOUT_DEVICE_BOOT_SEC) {
        if (find_and_open_target_device(dev)) {
            return true;
        }
        sleep(SLEEP_DEVICE_BOOT_SEC);
    }

    if (NULL != timeout_message) {
        fprintf(stderr, "%s.\n", timeout_message);
    }
    if (dev->ctx) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
    }

    return false;
}

static bool do_rfs_upload(const char *wait_timeout_message)
{
    device_t dev = {0};
    bool success = false;
    if (!wait_for_device_boot(&dev, wait_timeout_message)) {
        return false;
    }
    if (!validate_protocol_and_sku(&dev)) {
        fprintf(stderr, "Failed to validate protocol and SKU\n");
        goto exit;
    }
    char device_fw_version_buf[INFO_BUFFER_SIZE] = {0};
    if (!get_fw_version(&dev, REQ_GET_BUILD_INFO, EXPECTED_BUILD_INFO, "build info", device_fw_version_buf)) {
        fprintf(stderr, "Failed to validate build info\n");
        goto exit;
    }
    if (!write_version_file(device_fw_version_buf)) {
        fprintf(stderr, "Failed to write version file\n");
        goto exit;
    }

    if (!upload_image(&dev, PATH_RFS_OPERATIONAL)) {
        fprintf(stderr, "Failed to upload RFS operational image\n");
        goto exit;
    }

    // TODO: change to poll method
    sleep(SLEEP_RFS_SETTLE_SEC);
    success = true;

exit:
    device_close(&dev);
    return success;
}


static void best_effort_reset_device_to_wait_for_file_system(void)
{
    if (!usb_reset_device()) {
        fflush(stdout);
        fprintf(stderr,
            "Please remove and insert device. Waiting for 60 seconds. "
            "Press any key to abort...\n");
        if (compat_wait_for_keypress_or_timeout(60)) {
            fprintf(stderr, "Sleep aborted. Continuing...\n");
        }
    }

    // sleep to make sure that the reboot has taken affect and we dont get old device
    sleep(SLEEP_RESET_REBOOT_SEC);
}

static bool do_fw_update(void)
{
    // TODO: split into 3 funcs
    device_t dev = {0};
    bool success = false;

    printf("%s \n%s.\n%s... \n", LOADER_OUTPUT_STARTING, LOADER_OUTPUT_DONT_POWER_OFF, LOADER_OUTPUT_PLEASE_WAIT);

    report_fw_update_phase(FW_UPDATE_PHASE_RESTART_DEVICE);
    best_effort_reset_device_to_wait_for_file_system();

    if (!wait_for_device_boot(&dev, NULL)) {
        fprintf(stderr, "%s\n", LOADER_OUTPUT_RECONNECT_FAILED);
        goto exit;
    }
    if (!validate_protocol_and_sku(&dev)) {
        fprintf(stderr, "Failed to validate protocol and SKU at initial connection\n");
        goto exit;
    }

    report_fw_update_phase(FW_UPDATE_PHASE_UPLOAD_UPDATE_IMAGE);
    if (!upload_image(&dev, PATH_RFS_UPDATE)) {
        fprintf(stderr, "Failed to upload RFS update image\n");
        goto exit;
    }

    report_fw_update_phase(FW_UPDATE_PHASE_SWITCH_TO_UPDATE_MODE);
    device_close(&dev);
    sleep(SLEEP_CHANGE_MODE_SEC);

    if (!wait_for_device_boot(&dev, NULL)) {
        fprintf(stderr, "%s\n", LOADER_OUTPUT_RECONNECT_FAILED);
        goto exit;
    }

    report_fw_update_phase(FW_UPDATE_PHASE_UPLOAD_FW_PACKAGE);
    if (!upload_image(&dev, PATH_FWU_IMAGE)) {
        fprintf(stderr, "Failed to upload FWU image\n");
        goto exit;
    }

    report_fw_update_phase(FW_UPDATE_PHASE_INSTALL_FW);
    if (!wait_fwu_complete(&dev)) {
        fprintf(stderr, "Failed to complete FW update\n");
        goto exit;
    }

    report_fw_update_phase(FW_UPDATE_PHASE_RESTART_WITH_NEW_FW);
    // Device needs time to recover after the FW install. Without this, the reboot command fails.
    sleep(SLEEP_FWU_RECOVER_SEC);
    if (!reboot(&dev)) {
        fprintf(stderr, "Failed to reboot after uploading FWU image\n");
        goto exit;
    }
    device_close(&dev);

    if (!do_rfs_upload(LOADER_OUTPUT_UNPLUG_INSTRUCTION)) {
        return false; // No need to clean dev. It is handled inside
    }

    success = true;
exit:
    device_close(&dev);
    return success;
}

int main(int argc, char *argv[])
{
    compat_disable_stdout_buffering();
#ifndef _WIN32
    // A closed stdout/stderr reader must not kill the loader mid-update
    signal(SIGPIPE, SIG_IGN);
#endif

    if (argc != 2 || (strcmp(argv[1], "fw-update") != 0 && strcmp(argv[1], "rfs-upload") != 0)) {
        printf("Usage: %s <fw-update|rfs-upload>\n", argv[0]);
        return EXIT_FAILURE;
    }

    lock_handle_t lock = lock_acquire();
    if (LOCK_INVALID ==  lock) {
        return EXIT_FAILURE;
    }

    const bool is_fw_update = (0 == strcmp(argv[1], "fw-update"));
    bool success = is_fw_update ? do_fw_update() : do_rfs_upload(RFS_UPLOAD_DEVICE_TIMEOUT_MESSAGE);

    lock_release(lock);
    if (success) {
        printf("%s\n", LOADER_OUTPUT_COMPLETED);
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
