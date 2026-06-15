// SPDX-License-Identifier: GPL-2.0
/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 **/
/**
 * @file byte_order.h
 * @brief Defines byte-order operations.
**/

#ifndef __BYTE_ORDER_H__
#define __BYTE_ORDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type_utils.h"

/* Windows always runs on LE architectures. */
#if defined(_MSC_VER)
#define __ORDER_BIG_ENDIAN__    (0)
#define __ORDER_LITTLE_ENDIAN__ (1)
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
#endif

#if !defined(__BYTE_ORDER__)
#error "Byte-order is not defined!"
#endif

/* Firmware builds must target little-endian. */
#ifdef FIRMWARE_ARCH
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Firmware builds must target little-endian architecture"
#endif
#endif

#define BYTE_ORDER__swap16(n) ((((uint16_t)(n) & (uint16_t)0x00FFu) << 8) | (((uint16_t)(n) & (uint16_t)0xFF00u) >> 8))

#define BYTE_ORDER__swap32(n) \
        (((uint32_t)BYTE_ORDER__swap16((uint32_t)(n)) << 16) | ((uint32_t)BYTE_ORDER__swap16(((uint32_t)(n) >> 16))))

#define BYTE_ORDER__swap64(n) \
        (((uint64_t)BYTE_ORDER__swap32((uint64_t)(n)) << 32) | ((uint64_t)BYTE_ORDER__swap32(((uint64_t)(n) >> 32))))

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

/* host <-> little-endian: identity on LE host. */
#define BYTE_ORDER__htoles(n)  ((uint16_t)(n))
#define BYTE_ORDER__letohs(n)  ((uint16_t)(n))
#define BYTE_ORDER__htolel(n)  ((uint32_t)(n))
#define BYTE_ORDER__letohl(n)  ((uint32_t)(n))
#define BYTE_ORDER__htolell(n) ((uint64_t)(n))
#define BYTE_ORDER__letohll(n) ((uint64_t)(n))

/* host <-> big-endian: byte-swap on LE host. */
#define BYTE_ORDER__htobes(n)  BYTE_ORDER__swap16(n)
#define BYTE_ORDER__betohs(n)  BYTE_ORDER__swap16(n)
#define BYTE_ORDER__htobel(n)  BYTE_ORDER__swap32(n)
#define BYTE_ORDER__betohl(n)  BYTE_ORDER__swap32(n)
#define BYTE_ORDER__htobell(n) BYTE_ORDER__swap64(n)
#define BYTE_ORDER__betohll(n) BYTE_ORDER__swap64(n)

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

/* host <-> little-endian: byte-swap on BE host. */
#define BYTE_ORDER__htoles(n)  BYTE_ORDER__swap16(n)
#define BYTE_ORDER__letohs(n)  BYTE_ORDER__swap16(n)
#define BYTE_ORDER__htolel(n)  BYTE_ORDER__swap32(n)
#define BYTE_ORDER__letohl(n)  BYTE_ORDER__swap32(n)
#define BYTE_ORDER__htolell(n) BYTE_ORDER__swap64(n)
#define BYTE_ORDER__letohll(n) BYTE_ORDER__swap64(n)

/* host <-> big-endian: identity on BE host. */
#define BYTE_ORDER__htobes(n)  ((uint16_t)(n))
#define BYTE_ORDER__betohs(n)  ((uint16_t)(n))
#define BYTE_ORDER__htobel(n)  ((uint32_t)(n))
#define BYTE_ORDER__betohl(n)  ((uint32_t)(n))
#define BYTE_ORDER__htobell(n) ((uint64_t)(n))
#define BYTE_ORDER__betohll(n) ((uint64_t)(n))

#endif

/* host <-> network byte-order. Network byte-order is big-endian. */
#define BYTE_ORDER__htons(n)  BYTE_ORDER__htobes(n)
#define BYTE_ORDER__ntohs(n)  BYTE_ORDER__betohs(n)
#define BYTE_ORDER__htonl(n)  BYTE_ORDER__htobel(n)
#define BYTE_ORDER__ntohl(n)  BYTE_ORDER__betohl(n)
#define BYTE_ORDER__htonll(n) BYTE_ORDER__htobell(n)
#define BYTE_ORDER__ntohll(n) BYTE_ORDER__betohll(n)

/* host <-> device byte-order. Hailo devices byte-order is little endian. */
#define BYTE_ORDER__htods(n)  BYTE_ORDER__htoles(n)
#define BYTE_ORDER__dtohs(n)  BYTE_ORDER__letohs(n)
#define BYTE_ORDER__htodl(n)  BYTE_ORDER__htolel(n)
#define BYTE_ORDER__dtohl(n)  BYTE_ORDER__letohl(n)
#define BYTE_ORDER__htodll(n) BYTE_ORDER__htolell(n)
#define BYTE_ORDER__dtohll(n) BYTE_ORDER__letohll(n)

#ifdef __cplusplus
}
#endif

#endif /* __BYTE_ORDER_H__ */
