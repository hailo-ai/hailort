// SPDX-License-Identifier: GPL-2.0
/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 **/
/**
 * @file utils.h
 * @brief Defines common utilities.
**/

#ifndef __UTILS_H__
#define __UTILS_H__

#include "type_utils.h"

#define ARRAY_LENGTH(__array) (sizeof((__array)) / sizeof((__array)[0]))
#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#endif

#ifndef DIV_ROUND_DOWN
#define DIV_ROUND_DOWN(n,d) ((n) / (d))
#endif

#ifndef ROUND_UNSIGNED_FLOAT
#define ROUND_UNSIGNED_FLOAT(n) ((n - (uint32_t)(n)) > 0.5) ? (uint32_t)(n + 1) : (uint32_t)(n)
#endif

#ifndef IS_POWEROF2
#define IS_POWEROF2(v) ((v & (v - 1)) == 0)
#endif

#define CPU_CYCLES_NUMBER_IN_MS (configCPU_CLOCK_HZ / 1000)

#define GET_MASK(width, shift) (((1U << (width)) - 1U) << (shift))

#define MICROSECONDS_IN_MILLISECOND (1000)

static inline uint8_t ceil_log2(uint32_t n)
{
    uint8_t result = 0;

    if (n <= 1) {
        return 0;
    }

    while (n > 1) {
        result++;
        n = (n + 1) >> 1;
    }

    return result;
}

#endif /* __UTILS_H__ */
