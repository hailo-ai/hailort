/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file platform.h
 * @brief Platform dependent definitions for libhailopp
 **/

#ifndef _HAILOPP_PLATFORM_H_
#define _HAILOPP_PLATFORM_H_

/** Exported symbols define */

#if defined(_MSC_VER)
#if defined(_HAILOPP_EXPORTING)
#define HAILOPPAPI __declspec(dllexport)
#else
#define HAILOPPAPI __declspec(dllimport)
#endif
#else
#define HAILOPPAPI __attribute__ ((visibility ("default")))
#endif

#endif /* _HAILOPP_PLATFORM_H_ */
