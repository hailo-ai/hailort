/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel.c
 * @brief vDMA channel operations.
 **/

#include "vdma/channel.h"

#include "vdma/channel_regs.h"

#define VDMA_CHECK(cond, err) do { if (!(cond)) return err; } while (0)

#define PCIE_HW_CHAN_IDX_SHIFT     (16)
#define PCIE_DEVICE_CHAN_IDX_SHIFT (30)
#define PCIE_DEVICE_ADDRESS_MASK   ((1UL << PCIE_DEVICE_CHAN_IDX_SHIFT) - 1)

#define DRAM_CHAN_IDX_SHIFT       (57)
#define DRAM_DESC_LIST_TYPE_SHIFT (62)

#define INVALID_ENCODED_ADDR (0xFFFFFFFFFFFFFFFFU)

#define CHANNEL_MAX_DEPTH (16)

#define H2D_CHANNELS_COUNT          (16)
#define H2D_EXTENDED_CHANNELS_COUNT (24)
#define D2H_CHANNELS_COUNT          (16)

#define IS_PCIE_ENGINE(variant) (((variant) == VDMA_ENGINE_PCIE) || ((variant) == VDMA_ENGINE_PCIE_EP))

static uint8_t ceil_log2(uint32_t n)
{
    uint32_t remaining = (n > 0) ? (n - 1) : 0;
    uint8_t result = 0;

    while (remaining > 0) {
        result++;
        remaining >>= 1;
    }

    return result;
}

static uint8_t h2d_channels_count(enum vdma_engine_variant variant)
{
    return (variant == VDMA_ENGINE_DRAM_H12) ? H2D_EXTENDED_CHANNELS_COUNT : H2D_CHANNELS_COUNT;
}

uint8_t vdma_channels_count(enum vdma_engine_variant variant)
{
    return h2d_channels_count(variant) + D2H_CHANNELS_COUNT;
}

enum vdma_direction vdma_channel_direction_by_idx(enum vdma_engine_variant variant, uint8_t channel_idx)
{
    bool is_h2d = false;

    if (channel_idx >= vdma_channels_count(variant)) {
        return VDMA_DIRECTION_INVALID;
    }

    is_h2d = (channel_idx < h2d_channels_count(variant));

    /* For endpoint engines the channel-direction is inverted. */
    if (variant == VDMA_ENGINE_PCIE_EP) {
        is_h2d = !is_h2d;
    }

    return is_h2d ? VDMA_H2D : VDMA_D2H;
}

static bool descnum_equals(uint16_t num1, uint16_t num2, uint8_t depth)
{
    const uint16_t mask = (uint16_t)((1U << depth) - 1U);
    return ((num1 & mask) == (num2 & mask));
}

int vdma_channels_init(struct vdma_channels *channels, enum vdma_engine_variant variant, void vdma_iomem *channel_regs)
{
    VDMA_CHECK(variant < VDMA_ENGINE_VARIANT_COUNT, VDMA_ERR_INVALID_ENGINE);

    channels->variant = variant;
    channels->list = channel_regs;

    return VDMA_SUCCESS;
}

static uint8_t get_adjusted_index(const struct vdma_chan *chan)
{
    if ((vdma_channel_direction_by_idx(chan->variant, chan->index) == VDMA_H2D)) {
        return chan->index;
    }

    return (chan->index - h2d_channels_count(chan->variant));
}

static int get_channel(const struct vdma_channels *channels, uint8_t idx, bool is_src, struct vdma_chan *out_chan)
{
    VDMA_CHECK(idx < vdma_channels_count(channels->variant), VDMA_ERR_INVALID_CHANNEL_INDEX);

    out_chan->variant = channels->variant;
    out_chan->index = idx;
    out_chan->regs = is_src ? channels->list[idx].src : channels->list[idx].dest;

    return VDMA_SUCCESS;
}

int vdma_channel_src(const struct vdma_channels *channels, uint8_t idx, struct vdma_chan *out_chan)
{
    return get_channel(channels, idx, true, out_chan);
}

int vdma_channel_dest(const struct vdma_channels *channels, uint8_t idx, struct vdma_chan *out_chan)
{
    return get_channel(channels, idx, false, out_chan);
}

bool vdma_channel_is_started(const struct vdma_chan *chan)
{
    return vdma_get_control_bit(chan->regs, VDMA_CONTROL_BIT_START);
}

bool vdma_channel_is_paused(const struct vdma_chan *chan)
{
    return vdma_get_control_bit(chan->regs, VDMA_CONTROL_BIT_PAUSE);
}

enum vdma_direction vdma_channel_direction(const struct vdma_chan *chan)
{
    return vdma_channel_direction_by_idx(chan->variant, chan->index);
}

static uint64_t encode_pcie_desc_list_addr(uint64_t dma_addr, enum vdma_desc_list_type type, uint8_t chan_idx)
{
    switch (type) {
    case VDMA_DESC_LIST_HOST:
        return dma_addr;
    case VDMA_DESC_LIST_DEVICE_DRAM:
        return (dma_addr & PCIE_DEVICE_ADDRESS_MASK) | ((uint64_t)chan_idx << PCIE_DEVICE_CHAN_IDX_SHIFT);
    case VDMA_DESC_LIST_HARDWARE:
        return ((uint64_t)chan_idx << PCIE_HW_CHAN_IDX_SHIFT);
    default:
        return INVALID_ENCODED_ADDR;
    }
}

static uint64_t encode_dram_desc_list_addr(uint64_t dma_addr, enum vdma_desc_list_type type, uint8_t chan_idx)
{
    switch (type) {
    case VDMA_DESC_LIST_DEVICE_DRAM:
        return dma_addr | ((uint64_t)chan_idx << DRAM_CHAN_IDX_SHIFT) | ((uint64_t)type << DRAM_DESC_LIST_TYPE_SHIFT);
    case VDMA_DESC_LIST_CCB:
    case VDMA_DESC_LIST_HARDWARE:
        return ((uint64_t)chan_idx << DRAM_CHAN_IDX_SHIFT) | ((uint64_t)type << DRAM_DESC_LIST_TYPE_SHIFT);
    default:
        return INVALID_ENCODED_ADDR;
    }
}

static uint64_t encode_desc_list_addr(const struct vdma_chan *chan, const struct vdma_desc_list_params *desc_list)
{
    if (IS_PCIE_ENGINE(chan->variant)) {
        return encode_pcie_desc_list_addr(desc_list->dma_address, desc_list->type, chan->index);
    }

    return encode_dram_desc_list_addr(desc_list->dma_address, desc_list->type, get_adjusted_index(chan));
}

static uint8_t desc_list_data_id(enum vdma_desc_list_type type, enum vdma_engine_variant variant)
{
    if (!IS_PCIE_ENGINE(variant)) {
        return 0;
    }

    return (VDMA_DESC_LIST_HOST == type) ? VDMA_CHAN_DATA_ID_HOST : VDMA_CHAN_DATA_ID_DEVICE;
}

int vdma_channel_configure(const struct vdma_chan *chan, const struct vdma_desc_list_params *desc_list_params)
{
    uint8_t depth = ceil_log2(desc_list_params->count);
    uint32_t control_reg = 0;
    uint64_t encoded_addr = INVALID_ENCODED_ADDR;

    VDMA_CHECK(!vdma_channel_is_started(chan), VDMA_ERR_CHANNEL_RUNNING);
    VDMA_CHECK(vdma_channel_get_error(chan) == 0, VDMA_ERR_CHANNEL_ERROR);
    VDMA_CHECK((desc_list_params->dma_address & ~VDMA_DESCLIST_ADDR_MASK) == 0, VDMA_ERR_UNALIGNED_ADDRESS);
    VDMA_CHECK(depth > 0, VDMA_ERR_INVALID_DESC_COUNT);
    VDMA_CHECK(depth <= CHANNEL_MAX_DEPTH, VDMA_ERR_INVALID_DESC_COUNT);

    /* 0 indicates MAX_DEPTH */
    depth %= CHANNEL_MAX_DEPTH;

    encoded_addr = encode_desc_list_addr(chan, desc_list_params);
    VDMA_CHECK(encoded_addr != INVALID_ENCODED_ADDR, VDMA_ERR_INVALID);

    vdma_channel_write_desc_list_addr(chan->regs, encoded_addr);

    vdma_write_field(control_reg, VDMA_DESCNUM_AVAILABLE, 0);
    vdma_write_field(control_reg, VDMA_CHANNEL_DEPTH, depth);
    vdma_write_field(control_reg, VDMA_CHANNEL_DATA_ID, desc_list_data_id(desc_list_params->type, chan->variant));
    _vdma_write_reg(chan->regs, VDMA_REG_CONTROL, control_reg);

    return VDMA_SUCCESS;
}

void vdma_channel_start(const struct vdma_chan *chan)
{
    vdma_clear_control_bit(chan->regs, VDMA_CONTROL_BIT_PAUSE);
    vdma_set_control_bit(chan->regs, VDMA_CONTROL_BIT_START);
}

void vdma_channel_abort(const struct vdma_chan *chan)
{
    vdma_set_control_bit(chan->regs, VDMA_CONTROL_BIT_PAUSE);
    vdma_clear_control_bit(chan->regs, VDMA_CONTROL_BIT_START);
}

void vdma_channel_pause(const struct vdma_chan *chan)
{
    /* TODO (HRT-20998): Skipping this check causes errors in context-switch. */
    if (!vdma_get_control_bit(chan->regs, VDMA_CONTROL_BIT_PAUSE)) {
        vdma_set_control_bit(chan->regs, VDMA_CONTROL_BIT_PAUSE);
    }
}

void vdma_channel_resume(const struct vdma_chan *chan)
{
    /* TODO (HRT-20998) */
    if (vdma_get_control_bit(chan->regs, VDMA_CONTROL_BIT_PAUSE)) {
        vdma_clear_control_bit(chan->regs, VDMA_CONTROL_BIT_PAUSE);
    }
}

bool vdma_channel_is_idle(const struct vdma_chan *chan)
{
    /* NOTE: we read the register in one shot to avoid races. ONGOING and PROCESSED
     * are both updated by hardware so we may get a false result if we split the reads.
     * This is further evidence that this function should be removed. */
    const uint32_t prog_reg = _vdma_read_reg(chan->regs, VDMA_REG_PROGRESS);
    const uint16_t ongoing = vdma_read_field(prog_reg, VDMA_DESCNUM_ONGOING);
    const uint16_t processed = vdma_read_field(prog_reg, VDMA_DESCNUM_PROCESSED);

    return descnum_equals(ongoing, processed, vdma_channel_get_depth(chan));
}

bool vdma_channel_is_drained(const struct vdma_chan *chan)
{
    const uint16_t processed = vdma_read(chan->regs, VDMA_DESCNUM_PROCESSED);
    const uint16_t available = vdma_read(chan->regs, VDMA_DESCNUM_AVAILABLE);
    return descnum_equals(processed, available, vdma_channel_get_depth(chan));
}

uint8_t vdma_channel_get_depth(const struct vdma_chan *chan)
{
    const uint8_t depth = vdma_read(chan->regs, VDMA_CHANNEL_DEPTH);
    return (0 == depth) ? CHANNEL_MAX_DEPTH : depth;
}

uint16_t vdma_channel_get_available(const struct vdma_chan *chan)
{
    return vdma_read(chan->regs, VDMA_DESCNUM_AVAILABLE);
}

uint16_t vdma_channel_get_processed(const struct vdma_chan *chan)
{
    return vdma_read(chan->regs, VDMA_DESCNUM_PROCESSED);
}

uint16_t vdma_channel_get_ongoing(const struct vdma_chan *chan)
{
    return vdma_read(chan->regs, VDMA_DESCNUM_ONGOING);
}

void vdma_channel_set_available(const struct vdma_chan *chan, uint16_t available)
{
    vdma_write(chan->regs, VDMA_DESCNUM_AVAILABLE, available);
}

uint8_t vdma_channel_get_error(const struct vdma_chan *chan)
{
    return vdma_read(chan->regs, VDMA_CHANNEL_ERR);
}

uint32_t vdma_channel_pcie_desc_list_msbs(uint64_t desc_list_dma_address)
{
    return (uint32_t)(desc_list_dma_address >> PCIE_DEVICE_CHAN_IDX_SHIFT);
}
