/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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


/** Includes */

#if defined(_MSC_VER)
// Windows headers
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <ws2ipdef.h>
#include <afunix.h>
#else
// UNIX headers
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#endif


/** Typedefs */

// underlying_handle_t
#ifndef underlying_handle_t
#if defined(_MSC_VER)
typedef HANDLE underlying_handle_t;
#elif defined(__linux__) || defined(__QNX__)
typedef int underlying_handle_t;
#else
#error "Unsupported Platform"
#endif
#endif

// port_t
#ifndef port_t
#if defined(_MSC_VER)
typedef USHORT port_t;
#else
typedef in_port_t port_t;
#endif
#endif

// socket_t
#ifndef socket_t
#if defined(_MSC_VER)
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif
#endif

// timeval_t
#ifndef timeval_t
typedef struct timeval timeval_t;
#endif


/** Defines and Macros */

// TODO: Fix this hack
#ifndef MSG_CONFIRM
#define MSG_CONFIRM (0)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#if !defined(_MSC_VER) && !defined(INVALID_SOCKET)
// Already defined in Windows
#define INVALID_SOCKET (socket_t)(-1)
#endif

#if !defined(_MSC_VER) && !defined(SOCKET_ERROR)
// Already defined in Windows
#define SOCKET_ERROR (int)(-1)
#endif

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
