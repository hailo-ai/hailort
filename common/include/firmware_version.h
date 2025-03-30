/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file firmware_version.h
 * @brief Contains information regarding the firmware version.
**/

#ifndef __FIRMWARE_VERSION__
#define __FIRMWARE_VERSION__
#include <stdint.h>
typedef struct {
    uint32_t firmware_major;
    uint32_t firmware_minor;
    uint32_t firmware_revision;
} firmware_version_t;

#define PACK_FW_VERSION(major, minor, revision) {(major), (minor), (revision)}
#define MINIMUM_SECURED_FW_VERSION (firmware_version_t)PACK_FW_VERSION(2, 6, 0)

const firmware_version_t* FIRMWARE_VERSION__get_version(void);


#endif /* __FIRMWARE_VERSION__ */
