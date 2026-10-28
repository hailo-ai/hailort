/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file platform.h
 * @brief Platform-specific types and register access for the shared vDMA layer.
 **/

#ifndef _HAILO_VDMA_PLATFORM_H_
#define _HAILO_VDMA_PLATFORM_H_

/* To overwrite this file, write your own vdma_platform.h and include it in the system-headers. */
#if __has_include(<vdma_platform.h>)
#include <vdma_platform.h>
#else

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define vdma_iomem

static inline uint32_t vdma_platform_read32(const uint32_t *reg)
{
    return *(const volatile uint32_t *)reg;
}

static inline void vdma_platform_write32(uint32_t *reg, uint32_t value)
{
    *(volatile uint32_t *)reg = value;
}

#ifdef __cplusplus
}
#endif
#endif
#endif /* _HAILO_VDMA_PLATFORM_H_ */
