/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file descriptors.h
 * @brief vDMA descriptor layout and programming.
 **/

#ifndef _HAILO_VDMA_DESCRIPTORS_H_
#define _HAILO_VDMA_DESCRIPTORS_H_

#include "vdma/vdma_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VDMA_MIN_PAGE_SIZE  (0x40)
#define VDMA_MAX_PAGE_SIZE  (0x1000)
#define VDMA_MIN_DESC_COUNT (0x2)
#define VDMA_MAX_DESC_COUNT (0x10000)

#define VDMA_DESC_ADDR_MASK (~0x3FULL)

struct vdma_desc {
    uint32_t page_size_control;
    uint32_t addr_l_data_id;
    uint32_t addr_h;
    uint32_t remaining_page_size_status;
};

struct vdma_descs_ring {
    enum vdma_engine_variant variant;
    struct vdma_desc *start;
    struct vdma_desc *curr;
    struct vdma_desc *end;
    uint16_t page_size;
    uint16_t page_shift;
};

static inline uint32_t vdma_ring_size(const struct vdma_descs_ring *ring)
{
    return ring->end - ring->start;
}

int vdma_ring_init(struct vdma_descs_ring *ring, enum vdma_engine_variant variant, struct vdma_desc *descs,
    uint32_t ring_size, uint16_t page_size);

int vdma_ring_program(struct vdma_descs_ring *ring, uint8_t channel_idx, uint32_t size, uint64_t addr, bool req_irq);
int vdma_ring_program_residue(struct vdma_descs_ring *ring, uint32_t size, bool req_irq);
void vdma_ring_reset_desc(struct vdma_descs_ring *ring, uint16_t desc_num);

#ifdef __cplusplus
}
#endif
#endif /* _HAILO_VDMA_DESCRIPTORS_H_ */
