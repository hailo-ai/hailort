/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file byte_order.h
 * @brief Defines byte order operations.
**/

#ifndef __BYTE_ORDER_H__
#define __BYTE_ORDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#if !defined(__BYTE_ORDER__)
// TODO: Check this better?
#if defined(_MSC_VER)
#define __ORDER_LITTLE_ENDIAN__ (1)
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
#else
#error "Unexpected byte order"
#endif
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#define BYTE_ORDER__htonl(x) (x)
#define BYTE_ORDER__ntohs(x) (x)
#define BYTE_ORDER__ntohl(x) (x)
#define BYTE_ORDER__htons(x) (x)
#define BYTE_ORDER__ntohll(x) (x)
#define BYTE_ORDER__htonll(x) (x)


#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BYTE_ORDER__htons(n) ((uint16_t)((uint16_t)((((uint16_t)(n) & 0xFF)) << 8) | (((uint16_t)(n) & 0xFF00) >> 8)))
#define BYTE_ORDER__ntohs(n) ((uint16_t)((uint16_t)((((uint16_t)(n) & 0xFF)) << 8) | (((uint16_t)(n) & 0xFF00) >> 8)))

#define BYTE_ORDER__htonl(n) (((((uint32_t)(n) & 0xFF)) << 24) | \
                          ((((uint32_t)(n) & 0xFF00)) << 8) | \
                          ((((uint32_t)(n) & 0xFF0000)) >> 8) | \
                          ((((uint32_t)(n) & 0xFF000000)) >> 24))

#define BYTE_ORDER__ntohl(n) (((((uint32_t)(n) & 0xFF)) << 24) | \
                          ((((uint32_t)(n) & 0xFF00)) << 8) | \
                          ((((uint32_t)(n) & 0xFF0000)) >> 8) | \
                          ((((uint32_t)(n) & 0xFF000000)) >> 24))

#define BYTE_ORDER__htonll(n) (((uint64_t) BYTE_ORDER__htonl((n) & 0xFFFFFFFF) << 32) | \
                          (uint64_t) BYTE_ORDER__htonl((n) >> 32))

#define BYTE_ORDER__ntohll(n) (((uint64_t) BYTE_ORDER__htonl((n) & 0xFFFFFFFF) << 32) | \
                          (uint64_t) BYTE_ORDER__htonl((n) >> 32))

#endif 

#define BYTE_ORDER__switch_endiannessl(n) (((((uint32_t)(n) & 0xFF)) << 24) | \
                          ((((uint32_t)(n) & 0xFF00)) << 8) | \
                          ((((uint32_t)(n) & 0xFF0000)) >> 8) | \
                          ((((uint32_t)(n) & 0xFF000000)) >> 24))

#ifdef __cplusplus
}
#endif

#endif /* __BYTE_ORDER_H__ */
