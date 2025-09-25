/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file firmware_header_utils.h
 * @brief Utilities for working with the firmware header.
**/

#ifndef __FIRMWARE_HEADER_UTILS__
#define __FIRMWARE_HEADER_UTILS__
#include <stdint.h>
#include <stdbool.h>
#include "status.h"
#include "firmware_header.h"
#include "firmware_version.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REVISION_NUMBER_SHIFT (0)
#define REVISION_APP_CORE_FLAG_BIT_SHIFT (27)
#define REVISION_RESERVED_0_FLAG_BIT_SHIFT (28)
#define REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER_FLAG_BIT_SHIFT (29)
#define REVISION_DEV_FLAG_BIT_SHIFT (30)
#define REVISION_SECOND_STAGE_FLAG_BIT_SHIFT (31)

#define REVISION_NUMBER_WIDTH (27U)
#define REVISION_APP_CORE_FLAG_BIT_WIDTH (1U)
#define REVISION_RESERVED_0_FLAG_BIT_WIDTH (1U)
#define REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER_FLAG_BIT_WIDTH (1U)
#define REVISION_DEV_FLAG_BIT_WIDTH (1U)
#define REVISION_SECOND_STAGE_FLAG_BIT_WIDTH (1U)

#define REVISION_NUMBER_MASK (GET_MASK(REVISION_NUMBER_WIDTH, REVISION_NUMBER_SHIFT))
#define REVISION_APP_CORE_FLAG_BIT_MASK (GET_MASK(REVISION_APP_CORE_FLAG_BIT_WIDTH, REVISION_APP_CORE_FLAG_BIT_SHIFT))
#define REVISION_RESERVED_0_FLAG_BIT_MASK (GET_MASK(REVISION_RESERVED_0_FLAG_BIT_WIDTH, REVISION_RESERVED_0_FLAG_BIT_SHIFT))
#define REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER_FLAG_BIT_MASK (GET_MASK(REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER_FLAG_BIT_WIDTH, REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER_FLAG_BIT_SHIFT))
#define REVISION_DEV_FLAG_BIT_MASK (GET_MASK(REVISION_DEV_FLAG_BIT_WIDTH, REVISION_DEV_FLAG_BIT_SHIFT))
#define REVISION_SECOND_STAGE_FLAG_BIT_MASK (GET_MASK(REVISION_SECOND_STAGE_FLAG_BIT_WIDTH, REVISION_SECOND_STAGE_FLAG_BIT_SHIFT))

#define GET_REVISION_NUMBER_VALUE(binary_revision) (REVISION_NUMBER_MASK & binary_revision)
#define IS_REVISION_DEV(binary_revision) (REVISION_DEV_FLAG_BIT_MASK == (REVISION_DEV_FLAG_BIT_MASK & binary_revision))
#define IS_REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER(binary_revision) (REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER_FLAG_BIT_MASK == \
    (REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER_FLAG_BIT_MASK & binary_revision))
#define DEV_STRING_NOTE(__is_release) ((__is_release)? "" : " (dev)")

/**
* Validates the FW headers.
* this function is used from the firmware, from HailoRT and from the second_stage bootloader to validate the headers.
* @param[in] address                    Address of the firmware.
* @param[in] firmware_size              Size of the firmware.
*                                       If the actual size is unknown, set this to the maximum possible size (for example the size of the Flash
*                                       section holding the FW).
* @param[in] is_firmware_size_unknown   Set to true if the firmware size is unknown (for example when loading from Flash).
* @param[out] out_app_firmware_header   (optional) App firmware header
* @param[out] out_core_firmware_header  (optional) Core firmware header
* @param[out] out_firmware_cert         (optional) Firmware certificate header
*/
HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__validate_fw_headers(uintptr_t firmware_base_address,
                                                                 uint32_t firmware_size,
                                                                 bool is_firmware_size_unknown,
                                                                 firmware_header_t **out_app_firmware_header,
                                                                 firmware_header_t **out_core_firmware_header,
                                                                 secure_boot_certificate_t **out_firmware_cert,
                                                                 firmware_type_t firmware_type);
/**
* Validates the Second stage headers.
* this function is used from the firmware and from HailoRT to validate the headers.
* @param[in] address                        Address of the second stage.
* @param[in] second_stage_size              Size of the second_stage.
*                                           If the actual size is unknown, set this to the maximum possible size (for example the size of the Flash
*                                           section holding the Second stage).
* @param[out] out_second_stage_header       (optional) second_stage header
*/
HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__validate_second_stage_headers(uintptr_t second_stage_base_size,
                                                                           uint32_t second_stage_size,
                                                                           firmware_header_t **out_second_stage_header,
                                                                           firmware_type_t firmware_type);

/**
* Returns the binary type (App firmware, Core firmware or Second Stage boot)
* @param[in] binary_revision     The binary revision (Third part in binary version)
*/
FW_BINARY_TYPE_t FIRMWARE_HEADER_UTILS__get_fw_binary_type(uint32_t binary_revision);

/**
* Returns true if the new binary version is older then the minumum allowed.
* @param[in] new_binary_version               A pointer to the new binary version to update to
* @param[in] minimum_allowed_binary_version   A pointer to the minimum binary version that is allowed to be upgraded to
*/
HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__is_binary_being_downgraded(const firmware_version_t *new_binary_version, 
                                                                        const firmware_version_t *minimum_allowed_binary_version);

/**
* Validates the binary version to prevent downgrade.
* this function is used from the firmware and from HailoRT to prevent downgrade.
* @param[in] new_binary_version               A pointer to the new binary version to update to
* @param[in] minimum_allowed_binary_version   A pointer to the minimum binary version that is allowed to be upgraded to
* @param[in] fw_binary_type                   A bit that indicates which binary type is passed (app firmware, core firmware, second stage boot)
*/
HAILO_COMMON_STATUS_t FIRMWARE_HEADER_UTILS__validate_binary_version(const firmware_version_t *new_binary_version, 
                                                                     const firmware_version_t *minimum_allowed_binary_version,
                                                                     FW_BINARY_TYPE_t fw_binary_type);

#ifdef __cplusplus
}
#endif

#endif /* __FIRMWARE_HEADER_UTILS__ */
