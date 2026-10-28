/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel_regs.h
 * @brief vDMA channel register layout, field definitions, and register access.
 **/

#ifndef _HAILO_VDMA_CHANNEL_REGS_H_
#define _HAILO_VDMA_CHANNEL_REGS_H_

#include "vdma/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vdma_registers {
    VDMA_REG_CONTROL = 0, /* RW. start/stop/set-depth etc. */
    VDMA_REG_PROGRESS,    /* RO. Read descriptors-ongoing and descriptors-completed. */
    VDMA_REG_ADDR_L,      /* RW. Descriptor-list address LSBs. Byte 0 also indicates a channel-error. */
    VDMA_REG_ADDR_H,      /* RW. Descriptor-list address MSBs. */

    VDMA_REG_COUNT        /* Each channel-side has 4 32-bit wide registers. */
};

struct vdma_chan_regs {
    uint32_t src[VDMA_REG_COUNT];
    uint32_t dest[VDMA_REG_COUNT];
};

#define VDMA_CHANNEL_CONTROL_REG   VDMA_REG_CONTROL
#define VDMA_CHANNEL_CONTROL_SHIFT (0)
#define VDMA_CHANNEL_CONTROL_WIDTH (8)

#define VDMA_CONTROL_BIT_START             (0)
#define VDMA_CONTROL_BIT_PAUSE             (1)
#define VDMA_CONTROL_BIT_ABORT_ON_ERR      (2)
#define VDMA_CONTROL_BIT_IRQ_ON_ERR        (5)
#define VDMA_CONTROL_BIT_REPORT_AXI_ON_ERR (6)
#define VDMA_CONTROL_BIT_REPORT_PCI_ON_ERR (7)

/* Reserved on DRAM-vDMA engines. */
#define VDMA_CHANNEL_DATA_ID_REG   VDMA_REG_CONTROL
#define VDMA_CHANNEL_DATA_ID_SHIFT (8)
#define VDMA_CHANNEL_DATA_ID_WIDTH (3)

/* Describes the domain the descriptor-list sits in. */
#define VDMA_CHAN_DATA_ID_HOST   (0)
#define VDMA_CHAN_DATA_ID_DEVICE (4)

#define VDMA_CHANNEL_DEPTH_REG   VDMA_REG_CONTROL
#define VDMA_CHANNEL_DEPTH_SHIFT (11)
#define VDMA_CHANNEL_DEPTH_WIDTH (4)

#define VDMA_DESCNUM_AVAILABLE_REG   VDMA_REG_CONTROL
#define VDMA_DESCNUM_AVAILABLE_SHIFT (16)
#define VDMA_DESCNUM_AVAILABLE_WIDTH (16)

#define VDMA_DESCNUM_PROCESSED_REG   VDMA_REG_PROGRESS
#define VDMA_DESCNUM_PROCESSED_SHIFT (0)
#define VDMA_DESCNUM_PROCESSED_WIDTH (16)

#define VDMA_DESCNUM_ONGOING_REG   VDMA_REG_PROGRESS
#define VDMA_DESCNUM_ONGOING_SHIFT (16)
#define VDMA_DESCNUM_ONGOING_WIDTH (16)

/* The first byte of the ADDR_L register reports errors on the channel.
 * Each bit corresponds to a different error:
 *   - 0-2: reserved
 *   - 3: Data Error (ECRC, Poisoned, Memory Error or Write Data error)
 *   - 4: AXI Write SLVERR or DECERR
 *   - 5: PCIe Completion Timeout
 *   - 6: PCIe Completion UR or AXI Read SLVERR
 *   - 7: PCIe Completion CA or AXI Read DECERR
 *
 * Errors are W1C, so always write 0 to this register unless clearing an error.
 *   */
#define VDMA_CHANNEL_ERR_REG   VDMA_REG_ADDR_L
#define VDMA_CHANNEL_ERR_SHIFT (0)
#define VDMA_CHANNEL_ERR_WIDTH (8)

#define VDMA_DESCLIST_ADDR_L_REG   VDMA_REG_ADDR_L
#define VDMA_DESCLIST_ADDR_L_SHIFT (16)
#define VDMA_DESCLIST_ADDR_L_WIDTH (16)

#define VDMA_DESCLIST_ADDR_H_REG   VDMA_REG_ADDR_H
#define VDMA_DESCLIST_ADDR_H_SHIFT (0)
#define VDMA_DESCLIST_ADDR_H_WIDTH (32)

#define VDMA_DESCLIST_ADDR_MASK (~(((uint64_t)1ULL << VDMA_DESCLIST_ADDR_L_SHIFT) - 1))

#define _vdma_field_mask(shift, width) ((uint32_t)(((((uint64_t)1 << (width)) - 1)) << (shift)))

#define _vdma_read_reg(regs, reg) vdma_platform_read32(&(regs)[(reg)])
#define _vdma_write_reg(regs, reg, value) vdma_platform_write32(&(regs)[(reg)], value)

#define vdma_read_field(reg, FIELD) (((reg) & _vdma_field_mask(FIELD##_SHIFT, FIELD##_WIDTH)) >> FIELD##_SHIFT)
#define vdma_write_field(reg, FIELD, value) do { \
        uint32_t _vdma_mask = _vdma_field_mask(FIELD##_SHIFT, FIELD##_WIDTH); \
        (reg) = ((((uint32_t)(value) << FIELD##_SHIFT) & _vdma_mask) | ((reg) & ~_vdma_mask)); \
    } while(0)

#define vdma_read(regs, FIELD) (vdma_read_field(_vdma_read_reg((regs), FIELD##_REG), FIELD))
#define vdma_write(regs, FIELD, value) do { \
        uint32_t _vdma_tmp = _vdma_read_reg((regs), FIELD##_REG); \
        vdma_write_field(_vdma_tmp, FIELD, (value)); \
        _vdma_write_reg((regs), FIELD##_REG, _vdma_tmp); \
    } while(0)

static inline void vdma_channel_write_desc_list_addr(uint32_t vdma_iomem *regs, uint64_t addr)
{
    addr &= VDMA_DESCLIST_ADDR_MASK;
    _vdma_write_reg(regs, VDMA_DESCLIST_ADDR_L_REG, (uint32_t)addr);
    _vdma_write_reg(regs, VDMA_DESCLIST_ADDR_H_REG, (uint32_t)(addr >> 32));
}

static inline bool vdma_get_control_bit(const uint32_t vdma_iomem *regs, uint8_t bit)
{
    return ((vdma_read(regs, VDMA_CHANNEL_CONTROL) & (1 << bit)) != 0);
}

static inline void vdma_set_control_bit(uint32_t vdma_iomem *regs, uint8_t bit)
{
    uint32_t control_reg = _vdma_read_reg(regs, VDMA_CHANNEL_CONTROL_REG);
    const uint32_t control = vdma_read_field(control_reg, VDMA_CHANNEL_CONTROL);

    vdma_write_field(control_reg, VDMA_CHANNEL_CONTROL, control | (1u << bit));
    _vdma_write_reg(regs, VDMA_CHANNEL_CONTROL_REG, control_reg);
}

static inline void vdma_clear_control_bit(uint32_t vdma_iomem *regs, uint8_t bit)
{
    uint32_t control_reg = _vdma_read_reg(regs, VDMA_CHANNEL_CONTROL_REG);
    const uint32_t control = vdma_read_field(control_reg, VDMA_CHANNEL_CONTROL);

    vdma_write_field(control_reg, VDMA_CHANNEL_CONTROL, control & ~(1u << bit));
    _vdma_write_reg(regs, VDMA_CHANNEL_CONTROL_REG, control_reg);
}

#ifdef __cplusplus
}
#endif
#endif /* _HAILO_VDMA_CHANNEL_REGS_H_ */
