/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file platform.h
 * @brief Platform dependent includes and definitions
 **/

#ifndef _HAILO_PLATFORM_H_
#define _HAILO_PLATFORM_H_

#if !defined(_MSC_VER) && !defined(__GNUC__)
#error "OS must be defined (UNIX/WIN32)"
#endif


/** Exported symbols define */

#if defined(_MSC_VER)
#if defined(_HAILO_EXPORTING)
#define HAILORTAPI __declspec(dllexport)
#else
#define HAILORTAPI __declspec(dllimport)
#endif
#else
#define HAILORTAPI __attribute__ ((visibility ("default")))
#endif

/** Includes and Typedefs */
// underlying_handle_t
#ifndef underlying_handle_t
#if defined(__linux__) || defined(__QNX__)
#include <unistd.h>
typedef int underlying_handle_t;
#elif defined(_MSC_VER)
#include <windef.h>
typedef HANDLE underlying_handle_t;
#else
#error "Unsupported Platform"
#endif
#endif


/** Defines and Macros */

#ifdef __GNUC__
#define DEPRECATED(msg) __attribute((deprecated(msg)))
#else
#define DEPRECATED(msg)
#endif

#define EMPTY_STRUCT_PLACEHOLDER uint8_t reserved;

#ifndef MILLISECONDS_IN_SECOND
#define MILLISECONDS_IN_SECOND (1000)
#endif
#ifndef MICROSECONDS_IN_MILLISECOND
#define MICROSECONDS_IN_MILLISECOND (1000)
#endif

#endif /* _HAILO_PLATFORM_H_ */
