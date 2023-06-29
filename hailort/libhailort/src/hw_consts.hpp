/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hw_consts.hpp
 * @brief Hardware constants
 **/

#ifndef _HAILO_HW_CONSTS_HPP_
#define _HAILO_HW_CONSTS_HPP_

/** stable constants **************************************************/

/** Package constants *********************************************************/
#define HAILO8_INBOUND_DATA_STREAM_SIZE                                     (0x00010000L)
// Max periph bytes per buffer for hailo15 because (we use its value shifted right by 3 - according to the spec) to
// configure shmifo credit size - which in hailo15 only has a width of 10 bits
#define HAILO15_PERIPH_BYTES_PER_BUFFER_MAX_SIZE                            (0x00002000L)

/** PCIe constants and macors ************************************************/
#define PCIE_CONFIG_BASE_ADDRESS                                                        (0x00200000L)                                                                   // <hw_base_addresses_macros.h>::HW_BASE_ADDRESSES__PCIE_CONFIG(0, 0, 0)
#define PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__ATR_IMPL__SET(dst) (dst) =        ((dst) & ~0x00000001L) | ((uint32_t)(1) << 0)                                   // <pcie_bridge_config_macros.h>::PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__ATR_IMPL__SET
#define PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__ATR_SIZE__MODIFY(dst, src)        (dst) = ((dst) & ~0x0000007EL) | (((uint32_t)(src) << 1) & 0x0000007EL)         // <pcie_bridge_config_macros.h>::PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__ATR_SIZE__MODIFY
#define PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__SOURCE_ADDR__MODIFY(dst, src)     (dst) = ((dst) & ~0xFFFFF000L) | (((uint32_t)(src) << 12) & 0xFFFFF000L)        // <pcie_bridge_config_macros.h>::PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__SOURCE_ADDR__MODIFY
#define PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__ATR_IMPL__SET(dst) (dst) =        ((dst) & ~0x00000001L) | ((uint32_t)(1) << 0)                                   // <pcie_bridge_config_macros.h>::PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__ATR_IMPL__SET
#define PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__ATR_SIZE__MODIFY(dst, src)        (dst) = ((dst) & ~0x0000007EL) | (((uint32_t)(src) << 1) & 0x0000007EL)         // <pcie_bridge_config_macros.h>::PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__ATR_SIZE__MODIFY
#define PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__SOURCE_ADDR__MODIFY(dst, src)     (dst) = ((dst) & ~0xFFFFF000L) | (((uint32_t)(src) << 12) & 0xFFFFF000L)        // <pcie_bridge_config_macros.h>::PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__SOURCE_ADDR__MODIFY

/** Vdma Channel registers ***************************************************/
#define VDMA_CHANNEL_CONTROL_OFFSET         (0x00)
#define VDMA_CHANNEL_NUM_AVAIL_OFFSET       (0x02)
#define VDMA_CHANNEL_NUM_PROC_OFFSET        (0x04)


#endif /* _HAILO_HW_CONSTS_HPP_ */
