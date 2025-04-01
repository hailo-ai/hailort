/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file firmware_header_utils.c
 * @brief Utilities for working with the firmware header.
**/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "firmware_header.h"
#include "firmware_version.h"
#include "firmware_header_utils.h"
#include "utils.h"
#include "control_protocol.h"

/* when reading the firmware we don't want to read past the firmware_size,
   so we have a consumed_firmware_offset that is updated _before_ accessing data at that offset
   of firmware_base_address */
#define CONSUME_FIRMWARE(__size, __status) do {                                                 \
        consumed_firmware_offset += (uint32_t) (__size);                                        \
        if ((firmware_size < (__size)) || (firmware_size < consumed_firmware_offset)) {         \
            status = __status;                                                                  \
            goto exit;                                                                          \
        }                                                                                       \
    } while(0)

static HAILO_COMMON_STATUS_t firmware_header_utils__validate_fw_header(uintptr_t firmware_base_address,
                                                                       uint32_t firmware_size,
                                                                       uint32_t max_code_size,
                                                                       uint32_t *outer_consumed_firmware_offset,
                                                                       firmware_header_t **out_firmware_header,
                                                                       firmware_type_t firmware_type)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    firmware_header_t *firmware_header = NULL;
    uint32_t consumed_firmware_offset = *outer_consumed_firmware_offset;
    uint32_t firmware_magic = 0;

    firmware_header = (firmware_header_t *) (firmware_base_address + consumed_firmware_offset);
    CONSUME_FIRMWARE(sizeof(firmware_header_t), HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_FIRMWARE_HEADER_SIZE);

    switch (firmware_type) {
    case FIRMWARE_TYPE_HAILO8:
        firmware_magic = FIRMWARE_HEADER_MAGIC_HAILO8;
        break;
    case FIRMWARE_TYPE_HAILO15:
        firmware_magic = FIRMWARE_HEADER_MAGIC_HAILO15;
        break;
    case FIRMWARE_TYPE_HAILO15L:
        firmware_magic = FIRMWARE_HEADER_MAGIC_HAILO15L;
        break;
    case FIRMWARE_TYPE_MARS:
        firmware_magic = FIRMWARE_HEADER_MAGIC_MARS;
        break;
    default:
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_FIRMWARE_TYPE;
        goto exit;
    }

    if (firmware_magic != firmware_header->magic) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INCORRECT_FIRMWARE_HEADER_MAGIC;
        goto exit;
    }

    /* Validate that the firmware header version is supported */
    switch(firmware_header->header_version) {
        case FIRMWARE_HEADER_VERSION_INITIAL:
            break;
        default:
            status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__UNSUPPORTED_FIRMWARE__HEADER_VERSION;
            goto exit;
            break;
    }

    if (MINIMUM_FIRMWARE_CODE_SIZE > firmware_header->code_size) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__CODE_SIZE_BELOW_MINIMUM;
        goto exit;
    }

    if (max_code_size < firmware_header->code_size) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__CODE_OVERRUNS_RAM_SIZE;
        goto exit;
    }

    CONSUME_FIRMWARE(firmware_header->code_size, HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_FIRMWARE_CODE_SIZE);

    *outer_consumed_firmware_offset = consumed_firmware_offset;
    *out_firmware_header = firmware_header;
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

static HAILO_COMMON_STATUS_t firmware_header_utils__validate_cert_header(uintptr_t firmware_base_address,
                                                                         uint32_t firmware_size,
                                                                         uint32_t *outer_consumed_firmware_offset,
                                                                         secure_boot_certificate_t **out_firmware_cert)
{

    secure_boot_certificate_t *firmware_cert = NULL;
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    uint32_t consumed_firmware_offset = *outer_consumed_firmware_offset;

    firmware_cert = (secure_boot_certificate_t *) (firmware_base_address + consumed_firmware_offset);
    CONSUME_FIRMWARE(sizeof(secure_boot_certificate_t), HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_CERT_HEADER_SIZE);

    if ((MAXIMUM_FIRMWARE_CERT_KEY_SIZE < firmware_cert->key_size) ||
        (MAXIMUM_FIRMWARE_CERT_CONTENT_SIZE < firmware_cert->content_size)) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__CERT_TOO_LARGE;
        goto exit;
    }

    CONSUME_FIRMWARE(firmware_cert->key_size, HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_CERT_KEY_SIZE);
    CONSUME_FIRMWARE(firmware_cert->content_size, HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_CERT_CONTENT_SIZE);

    *outer_consumed_firmware_offset = consumed_firmware_offset;
    *out_firmware_cert = firmware_cert;
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__validate_fw_headers(uintptr_t firmware_base_address,
                                                                 uint32_t firmware_size,
                                                                 bool is_firmware_size_unknown,
                                                                 firmware_header_t **out_app_firmware_header,
                                                                 firmware_header_t **out_core_firmware_header,
                                                                 secure_boot_certificate_t **out_firmware_cert,
                                                                 firmware_type_t firmware_type)
{
    firmware_header_t *app_firmware_header = NULL;
    firmware_header_t *core_firmware_header = NULL;
    secure_boot_certificate_t *firmware_cert = NULL;
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    uint32_t consumed_firmware_offset = 0;

    status = firmware_header_utils__validate_fw_header(firmware_base_address, firmware_size, MAXIMUM_APP_FIRMWARE_CODE_SIZE,
        &consumed_firmware_offset, &app_firmware_header, firmware_type);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_APP_CPU_FIRMWARE_HEADER;
        goto exit;
    }

    status = firmware_header_utils__validate_cert_header(firmware_base_address, firmware_size,
        &consumed_firmware_offset, &firmware_cert);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_APP_CPU_FIRMWARE_CERTIFICATE_HEADER;
        goto exit;
    }

    status = firmware_header_utils__validate_fw_header(firmware_base_address, firmware_size, MAXIMUM_CORE_FIRMWARE_CODE_SIZE,
        &consumed_firmware_offset, &core_firmware_header, firmware_type);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_CORE_CPU_FIRMWARE_HEADER;
        goto exit;
    }

    if ((consumed_firmware_offset != firmware_size) && (!is_firmware_size_unknown)) {
        /* it is an error if there is leftover data after the last firmware header */
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__LEFTOVER_DATA_AFTER_LAST_FIRMWARE_HEADER;
        goto exit;
    }

    /* the out params are all optional */
    if (NULL != out_app_firmware_header) {
        *out_app_firmware_header = app_firmware_header;
    }
    if (NULL != out_firmware_cert) {
        *out_firmware_cert = firmware_cert;
    }
    if (NULL != out_core_firmware_header) {
        *out_core_firmware_header = core_firmware_header;
    }
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}


HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__validate_second_stage_headers(uintptr_t second_stage_base_size,
                                                                           uint32_t second_stage_size,
                                                                           firmware_header_t **out_second_stage_header,
                                                                           firmware_type_t firmware_type)
{
    firmware_header_t *second_stage_header = NULL;
    secure_boot_certificate_t *second_stage_cert = NULL;
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    uint32_t consumed_second_stage_offset = 0;

    status = firmware_header_utils__validate_fw_header(second_stage_base_size, second_stage_size, MAXIMUM_SECOND_STAGE_CODE_SIZE,
        &consumed_second_stage_offset, &second_stage_header, firmware_type);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_APP_CPU_FIRMWARE_HEADER;
        goto exit;
    }

    status = firmware_header_utils__validate_cert_header(second_stage_base_size, second_stage_size,
        &consumed_second_stage_offset, &second_stage_cert);
    if (HAILO_COMMON_STATUS__SUCCESS != status) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_APP_CPU_FIRMWARE_CERTIFICATE_HEADER;
        goto exit;
    }

    if (consumed_second_stage_offset != second_stage_size) {
        /* it is an error if there is leftover data after the last firmware header */
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__LEFTOVER_DATA_AFTER_LAST_FIRMWARE_HEADER;
        goto exit;
    }

    /* the out params are all optional */
    if (NULL != out_second_stage_header) {
        *out_second_stage_header = second_stage_header;
    }

    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}

FW_BINARY_TYPE_t FIRMWARE_HEADER_UTILS__get_fw_binary_type(uint32_t binary_revision)
{
    FW_BINARY_TYPE_t fw_binary_type = FW_BINARY_TYPE_INVALID;
    // Remove dev flag before checking binary type
    binary_revision &= ~(REVISION_DEV_FLAG_BIT_MASK);

    if (REVISION_SECOND_STAGE_FLAG_BIT_MASK == (binary_revision & REVISION_SECOND_STAGE_FLAG_BIT_MASK)) {
        fw_binary_type = FW_BINARY_TYPE_SECOND_STAGE_BOOT;
    } else if (0 == (binary_revision & (REVISION_APP_CORE_FLAG_BIT_MASK))) {
        fw_binary_type = FW_BINARY_TYPE_APP_FIRMWARE;
    } else if (REVISION_APP_CORE_FLAG_BIT_MASK == (binary_revision & (REVISION_APP_CORE_FLAG_BIT_MASK))) {
        fw_binary_type = FW_BINARY_TYPE_CORE_FIRMWARE;
    } else {
        fw_binary_type = FW_BINARY_TYPE_INVALID;
    }

    return fw_binary_type;
}

HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__is_binary_being_downgraded(const firmware_version_t *new_binary_version,
                                                                        const firmware_version_t *minimum_allowed_binary_version)
{
    bool is_binary_being_downgraded = 
            // Check if minimum allowed binary's major is greater than new binary's major
             (minimum_allowed_binary_version->firmware_major >  new_binary_version->firmware_major) ||
            // Check if minimum allowed binary's minor is greater than new binary's minor (If major is the same)
            ((minimum_allowed_binary_version->firmware_major == new_binary_version->firmware_major) &&
             (minimum_allowed_binary_version->firmware_minor >  new_binary_version->firmware_minor)) ||
            // Check if minimum allowed binary's revision is greater than new binary's revision (If major and minor are the same)
            ((minimum_allowed_binary_version->firmware_major == new_binary_version->firmware_major) &&
             (minimum_allowed_binary_version->firmware_minor == new_binary_version->firmware_minor) &&
             (GET_REVISION_NUMBER_VALUE(minimum_allowed_binary_version->firmware_revision) >
             (GET_REVISION_NUMBER_VALUE(new_binary_version->firmware_revision))));
    return is_binary_being_downgraded;
}

HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__validate_binary_version(const firmware_version_t *new_binary_version,
                                                                     const firmware_version_t *minimum_allowed_binary_version,
                                                                     FW_BINARY_TYPE_t fw_binary_type)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    // Make sure downgrade is not executed
    if (FIRMWARE_HEADER_UTILS__is_binary_being_downgraded(new_binary_version, minimum_allowed_binary_version)) {
            status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__DETECTED_PROHIBITED_DOWNGRADE_ATTEMPT;
            goto exit;
    }

    if (FIRMWARE_HEADER_UTILS__get_fw_binary_type(new_binary_version->firmware_revision) != fw_binary_type) {
        status = HAILO_STATUS__FIRMWARE_HEADER_UTILS__INVALID_BINARY_TYPE;
        goto exit;
    }

    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;

}