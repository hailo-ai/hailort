/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file descriptors.c
 * @brief vDMA descriptor programming.
 **/

#include "vdma/descriptors.h"

#define VDMA_CHECK(cond, err) do { if (!(cond)) return err; } while (0)

#define DESC_STATUS_REQ_ERR        (1 << 1)
#define DESC_REQUEST_IRQ_PROCESSED (1 << 2)
#define DESC_REQUEST_IRQ_ERR       (1 << 3)
#define DESC_IRQ_DEVICE            (1 << 4)
#define DESC_IRQ_HOST              (1 << 5)

#define DESC_PAGE_SIZE_SHIFT (8)
#define PAGE_SIZE_CTRL(page_size, intr) (((uint32_t)(page_size) << DESC_PAGE_SIZE_SHIFT) | DESC_STATUS_REQ_ERR | (intr))

#define CHANNEL_ID_SHIFT (57)

static const struct {
    uint64_t channel_id_mask;
    uint8_t data_id;
    uint32_t interrupt_bit;
} g_encodings[VDMA_ENGINE_VARIANT_COUNT] = {
    [VDMA_ENGINE_PCIE]     = { .channel_id_mask = 0x00, .data_id = 0, .interrupt_bit = DESC_IRQ_HOST },
    [VDMA_ENGINE_PCIE_EP]  = { .channel_id_mask = 0x00, .data_id = 6, .interrupt_bit = DESC_IRQ_DEVICE },
    [VDMA_ENGINE_DRAM_H15] = { .channel_id_mask = 0x0f, .data_id = 1, .interrupt_bit = DESC_IRQ_DEVICE },
    [VDMA_ENGINE_DRAM_H12] = { .channel_id_mask = 0x1f, .data_id = 1, .interrupt_bit = DESC_IRQ_DEVICE },
};

void vdma_ring_reset_desc(struct vdma_descs_ring *ring, uint16_t desc_num)
{
    ring->start[desc_num].page_size_control = PAGE_SIZE_CTRL(ring->page_size, 0);
}

int vdma_ring_init(struct vdma_descs_ring *ring, enum vdma_engine_variant variant, struct vdma_desc *descs,
    uint32_t ring_size, uint16_t page_size)
{
    const uint32_t default_page_size_ctrl = PAGE_SIZE_CTRL(page_size, 0);

    VDMA_CHECK(variant < VDMA_ENGINE_VARIANT_COUNT, VDMA_ERR_INVALID_ENGINE);
    VDMA_CHECK(ring_size >= VDMA_MIN_DESC_COUNT, VDMA_ERR_INVALID_DESC_COUNT);
    VDMA_CHECK(ring_size <= VDMA_MAX_DESC_COUNT, VDMA_ERR_INVALID_DESC_COUNT);
    VDMA_CHECK(page_size >= VDMA_MIN_PAGE_SIZE, VDMA_ERR_INVALID_PAGE_SIZE);
    VDMA_CHECK(page_size <= VDMA_MAX_PAGE_SIZE, VDMA_ERR_INVALID_PAGE_SIZE);
    VDMA_CHECK((page_size & (page_size - 1)) == 0, VDMA_ERR_INVALID_PAGE_SIZE);

    ring->variant = variant;
    ring->start = descs;
    ring->curr = descs;
    ring->end = descs;
    ring->page_size = page_size;
    ring->page_shift = 0;

    for (; ring->end != ring->start + ring_size; ring->end++) {
        ring->end->page_size_control = default_page_size_ctrl;
        ring->end->remaining_page_size_status = 0;
    }

    while ((page_size >>= 1) != 0) {
        ring->page_shift++;
    }

    return VDMA_SUCCESS;
}

static uint32_t interrupt_bits(enum vdma_engine_variant variant)
{
    return g_encodings[variant].interrupt_bit | DESC_REQUEST_IRQ_PROCESSED | DESC_REQUEST_IRQ_ERR;
}

static void set_last_size(struct vdma_descs_ring *ring, struct vdma_desc *last_desc, uint32_t chunk_size, bool req_irq)
{
    uint32_t residue = ((chunk_size - 1) & (ring->page_size - 1)) + 1;
    uint32_t intr_bits = req_irq ? interrupt_bits(ring->variant) : 0;

    last_desc->page_size_control = PAGE_SIZE_CTRL(residue, intr_bits);
}

/* Returns the last descriptor to program in a chunk, wrapped to the ring. */
static inline struct vdma_desc *last_desc(const struct vdma_descs_ring *ring, uint32_t size)
{
    const uint32_t descs_in_chunk = ((uint64_t)size + ring->page_size - 1) >> ring->page_shift;
    struct vdma_desc *last_desc = ring->curr + descs_in_chunk - 1;
    return (last_desc >= ring->end) ? last_desc - vdma_ring_size(ring) : last_desc;
}

int vdma_ring_program_residue(struct vdma_descs_ring *ring, uint32_t size, bool req_irq)
{
    struct vdma_desc *last = last_desc(ring, size);

    VDMA_CHECK(size > 0, VDMA_ERR_INVALID_CHUNK_SIZE);
    VDMA_CHECK(size <= (vdma_ring_size(ring) << ring->page_shift), VDMA_ERR_INVALID_CHUNK_SIZE);

    set_last_size(ring, last, size, req_irq);
    ring->curr = (last + 1 == ring->end) ? ring->start : last + 1;

    /* Return the index of the last descriptor, so the caller can easily clear it later. */
    return (last - ring->start);
}

static inline void program_run(struct vdma_desc *start, const struct vdma_desc *end, uint64_t *addr, uint32_t page_size)
{
    while (start != end) {
        start->addr_l_data_id = (uint32_t)*addr;
        start->addr_h = (uint32_t)(*addr >> 32);
        *addr += page_size;
        start++;
    }
}

static uint64_t encode_addr(enum vdma_engine_variant variant, uint8_t channel_index, uint64_t dma_address)
{
    const uint64_t channel_id = (uint64_t)(channel_index & g_encodings[variant].channel_id_mask);
    return (dma_address & VDMA_DESC_ADDR_MASK) | (channel_id << CHANNEL_ID_SHIFT) | g_encodings[variant].data_id;
}

int vdma_ring_program(struct vdma_descs_ring *ring, uint8_t channel_idx, uint32_t size, uint64_t addr, bool req_irq)
{
    struct vdma_desc *last = last_desc(ring, size);

    VDMA_CHECK((addr & ~VDMA_DESC_ADDR_MASK) == 0, VDMA_ERR_UNALIGNED_ADDRESS);
    VDMA_CHECK(size > 0, VDMA_ERR_INVALID_CHUNK_SIZE);
    VDMA_CHECK(size <= (vdma_ring_size(ring) << ring->page_shift), VDMA_ERR_INVALID_CHUNK_SIZE);

    addr = encode_addr(ring->variant, channel_idx, addr);

    if (last < ring->curr) {
        program_run(ring->curr, ring->end, &addr, ring->page_size);
        ring->curr = ring->start;
    }

    program_run(ring->curr, last + 1, &addr, ring->page_size);
    set_last_size(ring, last, size, req_irq);
    ring->curr = (last + 1 == ring->end) ? ring->start : last + 1;

    /* Return the index of the last descriptor, so the caller can easily clear it later. */
    return (last - ring->start);
}
