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

#if !defined(_WIN32) && !defined(__linux__) && !defined(__QNX__)
#error "Unsupported platform (expected Windows, Linux, or QNX)"
#endif


/** Exported symbols define */

#if defined(_WIN32)
#if defined(_HAILO_EXPORTING)
#define HAILORTAPI __declspec(dllexport)
#else
#define HAILORTAPI __declspec(dllimport)
#endif
#else
#define HAILORTAPI __attribute__ ((visibility ("default")))
#endif

/*
 * Windows SDK arch macros (_AMD64_, _X86_, _ARM64_) must be defined before
 * including any SDK header (e.g. winnt.h, windef.h, windows.h).
 * This block maps compiler-specific arch macros (_M_* for MSVC,
 * __x86_64__/etc. for GCC/Clang) to the SDK-expected macros.
 *
 * If a Windows SDK header was already included before this file,
 * the arch macros were missing when they were needed — fail early.
 */
#if defined(_WINNT_) && !defined(_AMD64_) && !defined(_X86_) && !defined(_ARM64_)
#error "Windows SDK included before hailo/platform.h - include platform.h first"
#endif
#if defined(_WIN32)
#if (defined(_M_AMD64) || defined(__x86_64__)) && !defined(_AMD64_)
#define _AMD64_ 1
#elif (defined(_M_IX86) || defined(__i386__)) && !defined(_X86_)
#define _X86_ 1
#elif (defined(_M_ARM64) || defined(__aarch64__)) && !defined(_ARM64_)
#define _ARM64_ 1
#endif
#endif

/*
 * Prevent windows.h from defining min/max macros that conflict with
 * std::min / std::max and other standard library uses.
 */
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

/** Includes and Typedefs */
// underlying_handle_t
#ifndef underlying_handle_t
#if defined(__linux__) || defined(__QNX__)
#include <unistd.h>
typedef int underlying_handle_t;
#elif defined(_WIN32)
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
