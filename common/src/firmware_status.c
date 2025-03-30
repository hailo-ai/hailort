/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file firmware_status.c
 * @brief Defines firmware status codes.
**/

#include <stdint.h>
#include "firmware_status.h"
#include <string.h>

#ifdef FIRMWARE_ARCH

#pragma pack(push, 1)
typedef struct {
    const char *status_name;
    uint32_t status_id;
} FIRMWARE_STATUS__status_record_t;

typedef struct {
    const char *module_name;
    uint32_t module_id;
} FIRMWARE_STATUS__module_record_t;
#pragma pack(pop)

#define FIRMWARE_STATUS_SECTION  __attribute__((section(".firmware_statuses")))

#define FIRMWARE_MODULE__X(module) static const char module##_str[] FIRMWARE_STATUS_SECTION = #module;
#define FIRMWARE_STATUS__X(name) static const char name##_str[] FIRMWARE_STATUS_SECTION = #name;
FIRMWARE_STATUS__VARIABLES
#undef FIRMWARE_STATUS__X
#undef FIRMWARE_MODULE__X

const FIRMWARE_STATUS__module_record_t FIRMWARE_STATUS__module_records[] FIRMWARE_STATUS_SECTION = {
#define FIRMWARE_MODULE__X(module) { .module_id = module, .module_name = module##_str },
#define FIRMWARE_STATUS__X(name)
    FIRMWARE_STATUS__VARIABLES
#undef FIRMWARE_STATUS__X
#undef FIRMWARE_MODULE__X
};

const FIRMWARE_STATUS__status_record_t FIRMWARE_STATUS__status_records[] FIRMWARE_STATUS_SECTION = {
#define FIRMWARE_MODULE__X(module)
#define FIRMWARE_STATUS__X(name) { .status_id = name, .status_name = name##_str },
    FIRMWARE_STATUS__VARIABLES
#undef FIRMWARE_STATUS__X
#undef FIRMWARE_MODULE__X
};

#endif

#ifndef FIRMWARE_ARCH

static const char *FIRMWARE_STATUS__textual_format[] = 
{
#define FIRMWARE_MODULE__X(module)
#define FIRMWARE_STATUS__X(name) #name,
    FIRMWARE_STATUS__VARIABLES
#undef FIRMWARE_STATUS__X
#undef FIRMWARE_MODULE__X
};

/* The FIRMWARE_STATUS__textual_format array stores the strings in "absolute" order.
   In order for us to know the absolute index of each status we store an array that for each module stores 
   the absolute index of it's first status.
   This way we can compute the absolute index in O(1) time. */

#define HELPER_INDEX_NAME(__name) __HELPER_FIRMWARE_STATUS__##__name

/* the helper indices counts all module names and statuses.
   the goal here is to be able to count how many statuses occured prior to each module.
   we mark the module starts with the module__START elements, and in order 
   to calculate the number of statuses prior to the current module, we take the
   module's module__START value, and subtract the number of __START element
   of previous modules, which is the enum value of the module. */
typedef enum {
#define FIRMWARE_MODULE__X(module) HELPER_INDEX_NAME(module##__START),
#define FIRMWARE_STATUS__X(name) HELPER_INDEX_NAME(name), 
    FIRMWARE_STATUS__VARIABLES
#undef FIRMWARE_STATUS__X
#undef FIRMWARE_MODULE__X
} __FIRMWARE_STATUS__helper_indices_t;

static const uint32_t FIRMWARE_STATUS__absolute_module_indices[] = {
#define FIRMWARE_MODULE__X(module) HELPER_INDEX_NAME(module##__START) - module,
#define FIRMWARE_STATUS__X(name)
    FIRMWARE_STATUS__VARIABLES
#undef FIRMWARE_STATUS__X
#undef FIRMWARE_MODULE__X
    ARRAY_LENGTH(FIRMWARE_STATUS__textual_format)
};

HAILO_COMMON_STATUS_t FIRMWARE_STATUS__get_textual(FIRMWARE_STATUS_t fw_status, const char **text)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    uint32_t module_id = 0;
    uint32_t module_abs_index = 0;
    uint32_t next_module_abs_index = 0;
    uint32_t status_value = 0;

    if (NULL == text) {
        status = HAILO_STATUS__FIRMWARE_STATUS__NULL_ARGUMENT_PASSED;
        goto exit;
    }

    if (FIRMWARE_STATUS__COMPONENT_ID != FIRMWARE_STATUS__COMPONENT_GET(fw_status)) {
        status = HAILO_STATUS__FIRMWARE_STATUS__INVALID_COMPONENT_ID;
        goto exit;
    }

    module_id = FIRMWARE_STATUS__MODULE_INDEX_GET(fw_status);
    if (FIRMWARE_MODULE_COUNT <= module_id) {
        status = HAILO_STATUS__FIRMWARE_STATUS__INVALID_MODULE_ID;
        goto exit;
    }

    module_abs_index = FIRMWARE_STATUS__absolute_module_indices[module_id];
    next_module_abs_index = FIRMWARE_STATUS__absolute_module_indices[module_id+1];
    status_value = (uint32_t)FIRMWARE_STATUS__VALUE_GET(fw_status) - 1; /* status values start at 1 */
    
    /* check status value is in the correct range */
    if (status_value >= next_module_abs_index - module_abs_index) {
        status = HAILO_STATUS__FIRMWARE_STATUS__INVALID_STATUS_VALUE;
        goto exit;
    }
    
    *text = FIRMWARE_STATUS__textual_format[module_abs_index + status_value];
    status = HAILO_COMMON_STATUS__SUCCESS;

exit:
    return status;
}
#endif
