/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_types.h
 * @brief Definitions shared across the vDMA module.
 **/

#ifndef _HAILO_VDMA_TYPES_H_
#define _HAILO_VDMA_TYPES_H_

#include "vdma/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VDMA_SUCCESS                   (0)
#define VDMA_ERR_INVALID               (-1)
#define VDMA_ERR_CHANNEL_RUNNING       (-2)
#define VDMA_ERR_CHANNEL_ERROR         (-3)
#define VDMA_ERR_UNALIGNED_ADDRESS     (-4)
#define VDMA_ERR_INVALID_DESC_COUNT    (-5)
#define VDMA_ERR_INVALID_CHANNEL_INDEX (-6)
#define VDMA_ERR_INVALID_ENGINE        (-7)
#define VDMA_ERR_INVALID_PAGE_SIZE     (-8)
#define VDMA_ERR_INVALID_CHUNK_SIZE    (-9)

enum vdma_direction {
    VDMA_H2D = 0,
    VDMA_D2H,
    VDMA_DIRECTION_INVALID,
};

enum vdma_engine_variant {
    VDMA_ENGINE_PCIE = 0, /* Host side of a PCIe link */
    VDMA_ENGINE_PCIE_EP,  /* Device side of a PCIe link */
    VDMA_ENGINE_DRAM_H15,
    VDMA_ENGINE_DRAM_H12,
    VDMA_ENGINE_VARIANT_COUNT,
};

#ifdef __cplusplus
}
#endif
#endif /* _HAILO_VDMA_TYPES_H_ */
