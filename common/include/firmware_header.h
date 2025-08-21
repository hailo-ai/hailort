/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file firmware_header.h
 * @brief Contains definitions needed for generating and handling a firmware header.
**/

#ifndef __FIRMWARE_HEADER__
#define __FIRMWARE_HEADER__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "utils.h"

#define FIRMWARE_HEADER_MAGIC_HAILO8    (0x1DD89DE0)
#define FIRMWARE_HEADER_MAGIC_HAILO15   (0xE905DAAB)
#define FIRMWARE_HEADER_MAGIC_HAILO15L  (0xF94739AB)
#define FIRMWARE_HEADER_MAGIC_MARS      (0x8639AA42)

typedef enum {
    FIRMWARE_HEADER_VERSION_INITIAL = 0,

    /* MUST BE LAST */
    FIRMWARE_HEADER_VERSION_COUNT
} firmware_header_version_t;

typedef enum {
    FIRMWARE_TYPE_HAILO8 = 0,
    FIRMWARE_TYPE_HAILO15,
    FIRMWARE_TYPE_HAILO15L,
    FIRMWARE_TYPE_MARS
} firmware_type_t;

typedef struct {
    uint32_t magic;
    uint32_t header_version;
    uint32_t firmware_major;
    uint32_t firmware_minor;
    uint32_t firmware_revision;
    uint32_t code_size;
} firmware_header_t;

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t key_size;
    uint32_t content_size;
    uint8_t certificates_data[0];
} secure_boot_certificate_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif


#define MINIMUM_FIRMWARE_CODE_SIZE (20*4)
// Tightly coupled with ld script
#define MAXIMUM_APP_FIRMWARE_CODE_SIZE (0x40000)
#define MAXIMUM_CORE_FIRMWARE_CODE_SIZE (0x18000)
#define MAXIMUM_SECOND_STAGE_CODE_SIZE (0x80000)
#define MAXIMUM_FIRMWARE_CERT_KEY_SIZE (0x1000)
#define MAXIMUM_FIRMWARE_CERT_CONTENT_SIZE (0x1000)

typedef enum {
    FW_BINARY_TYPE_INVALID = 0,
    FW_BINARY_TYPE_APP_FIRMWARE,
    FW_BINARY_TYPE_CORE_FIRMWARE,
    FW_BINARY_TYPE_SECOND_STAGE_BOOT
} FW_BINARY_TYPE_t;

#ifdef __cplusplus
}
#endif

#endif /* __FIRMWARE_HEADER__ */
