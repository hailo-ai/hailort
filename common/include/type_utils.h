// SPDX-License-Identifier: GPL-2.0
/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 **/
/**
 * @file type_utils.h
 * @brief Defines common types.
**/

#ifndef __TYPE_UTILS_H__
#define __TYPE_UTILS_H__

#ifdef __KERNEL__ /* Common Types for Linux Kernel */

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/string.h>

#define UINT8_MAX U8_MAX

#else
	
#ifdef _KERNEL_MODE /* Common Types for Windows Kernel */

#include <wdm.h>
#include <stdbool.h>
#include <limits.h>

typedef UINT8  uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;

typedef INT8  int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;

#else /* Common Types for Windows + Linux Userspace */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>

#endif

#endif

#include "stdfloat.h"

#endif /* __TYPE_UTILS_H__ */
