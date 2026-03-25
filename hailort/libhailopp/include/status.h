/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file status.h
 * @brief Status codes for libhailopp
 **/

#ifndef _HAILOPP_STATUS_H_
#define _HAILOPP_STATUS_H_

#include <limits.h>

/** @defgroup group_hailopp_defines HailoPP API definitions
 *  @{
 */

/** HailoPP return codes */
#define HAILOPP_STATUS_VARIABLES\
    HAILOPP_STATUS__X(0,  HAILOPP_SUCCESS              /*!< Success - No error */)\
    HAILOPP_STATUS__X(1,  HAILOPP_INVALID_ARGUMENT     /*!< Invalid argument passed to function */)\
    HAILOPP_STATUS__X(2,  HAILOPP_OUT_OF_HOST_MEMORY   /*!< Cannot allocate more memory at host */)\
    HAILOPP_STATUS__X(3,  HAILOPP_UNINITIALIZED        /*!< Not initialized */)\

typedef enum {
#define HAILOPP_STATUS__X(value, name) name = value,
    HAILOPP_STATUS_VARIABLES
#undef HAILOPP_STATUS__X

    /** Must be last */
    HAILOPP_STATUS_COUNT,

    /** Max enum value to maintain ABI integrity */
    HAILOPP_STATUS_MAX_ENUM      = INT_MAX
} hailopp_status;

/** @} */

#endif /* _HAILOPP_STATUS_H_ */
