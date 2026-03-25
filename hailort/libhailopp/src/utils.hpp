/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file utils.hpp
 * @brief Simplified utility macros for libhailopp
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
 * @brief Check if argument is not null, return HAILOPP_INVALID_ARGUMENT if null
 * 
 * @param arg Argument to check
 **/
#define CHECK_NOT_NULL(arg) \
    CHECK((nullptr != (arg)), HAILOPP_INVALID_ARGUMENT)

/**
 * @brief Check hailopp_status, return it if not HAILOPP_SUCCESS
 **/
#define CHECK_SUCCESS(status) \
    CHECK((HAILOPP_SUCCESS == (status)), (status))

/**
 * @brief Check Expected, return the error as hailopp_status if unexpected
 **/
#define CHECK_EXPECTED_AS_STATUS(expr) \
    do { \
        const auto &_check_expected_result = (expr); \
        if (!_check_expected_result.has_value()) { \
            return _check_expected_result.error(); \
        } \
    } while(0)

#endif // _HAILOTRACKER_SRC_UTILS_HPP_

