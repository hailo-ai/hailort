/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file utils.hpp
 * @brief Simplified utility macros for libhailotracker
 **/ 

#ifndef _HAILOTRACKER_SRC_UTILS_HPP_
#define _HAILOTRACKER_SRC_UTILS_HPP_

#include "hailotracker.h"

/**
 * @brief Check condition and return error status if false
 * 
 * @param cond Condition to check
 * @param status Error status to return if condition is false
 **/
#define CHECK(cond, status) \
    do { \
        if (!(cond)) { \
            return (status); \
        } \
    } while(0)

/**
 * @brief Check if argument is not null, return HAILO_INVALID_ARGUMENT if null
 * 
 * @param arg Argument to check
 **/
#define CHECK_NOT_NULL(arg) \
    CHECK((nullptr != (arg)), HAILO_INVALID_ARGUMENT)

#endif // _HAILOTRACKER_SRC_UTILS_HPP_

