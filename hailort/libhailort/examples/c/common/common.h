/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file example_common.h
 * Common macros and defines used by Hailort Examples
 **/

#ifndef _EXAMPLE_COMMON_H_
#define _EXAMPLE_COMMON_H_

#include <stdio.h>
#include <stdlib.h>


#define FREE(var)                           \
    do {                                    \
        if (NULL != (var)) {                \
            free(var);                      \
            var = NULL;                     \
        }                                   \
    } while(0)

#define REQUIRE_ACTION(cond, action, label, ...) \
    do {                                         \
        if (!(cond)) {                           \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
            action;                              \
            goto label;                          \
        }                                        \
    } while(0)

#define REQUIRE_SUCCESS(status, label, ...) REQUIRE_ACTION((HAILO_SUCCESS == (status)), , label, __VA_ARGS__)

#define ARRAY_LENGTH(__array) (sizeof((__array)) / sizeof((__array)[0]))


#if defined(__unix__)
#define hailo_sleep(seconds) sleep((seconds))
#elif defined(_MSC_VER)
#define hailo_sleep(seconds) Sleep((seconds) * 1000)
#else /* defined(_MSC_VER) */
#pragma error("sleep not supported")
#endif


#endif /* _EXAMPLE_COMMON_H_ */
