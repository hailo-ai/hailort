/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file logger_level.h
 * @brief Contains the possible logger level information.
**/

#ifndef __LOGGER_LEVEL__
#define __LOGGER_LEVEL__

typedef enum {
    FW_LOGGER_LEVEL_TRACE = 0,
    FW_LOGGER_LEVEL_DEBUG,
    FW_LOGGER_LEVEL_INFO,
    FW_LOGGER_LEVEL_WARN,
    FW_LOGGER_LEVEL_ERROR,
    FW_LOGGER_LEVEL_FATAL
} FW_LOGGER_LEVEL_t;

#endif //__LOGGER_LEVEL__