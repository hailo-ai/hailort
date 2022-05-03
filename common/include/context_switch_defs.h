/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file context_switch_defs.h
 * @brief Declerations of all context switch related structs.
**/

#ifndef __CONTEXT_SWITCH_DEFS__
#define __CONTEXT_SWITCH_DEFS__

#ifdef __cplusplus
extern "C" {
#endif

#include "control_protocol.h"

/**********************************************************************
 * Defines
 **********************************************************************/

#define CONTEXT_SWITCH_DEFS__TIMESTAMP_INIT_VALUE (0xFFFFFFFF)
#define CONTEXT_SWITCH_DEFS__ENABLE_LCU_DEFAULT_KERNEL_ADDRESS (1)
#define CONTEXT_SWITCH_DEFS__ENABLE_LCU_DEFAULT_KERNEL_COUNT (2)

#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_SHIFT (0)
#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_WIDTH (4)
#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_MASK (0x0f)
#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_READ(src) \
    (((uint8_t)(src) & CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_MASK) >> CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_SHIFT)

#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_SHIFT (CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_WIDTH)
#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_MASK (0x70)
#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_READ(src) \
    (((uint8_t)(src) & CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_MASK) >> CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_SHIFT)

#define CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_SET(dst, cluster_index, lcu_index)                                                       \
    (dst) = (((lcu_index) << CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_SHIFT) & CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_LCU_INDEX_MASK) |  \
    (((cluster_index) << CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_SHIFT) & CONTEXT_SWITCH_DEFS__PACKED_LCU_ID_CLUSTER_INDEX_MASK)


#pragma pack(push, 1)
typedef struct {
    uint16_t core_bytes_per_buffer;
    uint16_t core_buffers_per_frame;
    uint16_t periph_bytes_per_buffer;
    uint16_t periph_buffers_per_frame;
    uint16_t feature_padding_payload;
    uint16_t buffer_padding_payload;
    uint16_t buffer_padding;
} CONTEXT_SWITCH_DEFS__stream_reg_info_t;

#if defined(_MSC_VER)
typedef enum : uint8_t {
#else
typedef enum __attribute__((packed)) {
#endif
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_VDMA_DESCRIPTORS = 0,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_TRIGGER_SEQUENCER,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_DATA_FROM_VDMA_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ENABLE_LCU_DEFAULT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ENABLE_LCU_NON_DEFAULT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_DISABLE_LCU,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_BOUNDARY_INPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_BOUNDARY_OUTPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_INTER_CONTEXT_INPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_INTER_CONTEXT_OUTPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_DDR_BUFFER_INPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_DDR_BUFFER_OUTPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_DEACTIVATE_VDMA_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_CHANGE_VDMA_TO_STREAM_MAPPING,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ADD_DDR_PAIR_INFO,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_DDR_BUFFERING_START,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_LCU_INTERRUPT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_SEQUENCER_DONE_INTERRUPT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_INPUT_CHANNEL_TRANSFER_DONE_INTERRUPT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_OUTPUT_CHANNEL_TRANSFER_DONE_INTERRUPT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_MODULE_CONFIG_DONE_INTERRUPT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_APPLICATION_CHANGE_INTERRUPT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_CFG_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_DEACTIVATE_CFG_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_REPEATED_ACTION,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_WAIT_FOR_DMA_IDLE_ACTION,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_WAIT_FOR_NMS_IDLE,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_CCW_BURSTS,
    
    /* Must be last */
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_COUNT
} CONTEXT_SWITCH_DEFS__ACTION_TYPE_t;

typedef struct {
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_t action_type;
    uint32_t time_stamp;
} CONTEXT_SWITCH_DEFS__common_action_header_t;

/**
 * The layout of a repeated action in the action_list will be as follows:
 * 1) CONTEXT_SWITCH_DEFS__common_action_header_t with 'action_type' CONTEXT_SWITCH_DEFS__ACTION_TYPE_REPEATED_ACTION
 * 2) CONTEXT_SWITCH_DEFS__repeated_action_header_t
 * 3) 'count' sub-actions whose type matches the 'sub_action_type' defined by (1).
 *    The sub-actions will be consecutive, and won't have 'CONTEXT_SWITCH_DEFS__common_action_header_t's
 * 
 * E.g - 3 repeated 'CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t's:
 * |-------------------------------------------------------------------------------------------------------|
 * | action_list |                                        data                                             |
 * |-------------------------------------------------------------------------------------------------------|
 * |    ...      |                                                                                         |
 * |     |       | CONTEXT_SWITCH_DEFS__common_action_header_t {                                           |
 * |     |       |     .action_type = CONTEXT_SWITCH_DEFS__ACTION_TYPE_REPEATED_ACTION;                    |
 * |     |       |     .time_stamp = <time_of_last_executed_action_in_repeated>;                           |
 * |     |       | }                                                                                       |
 * |     |       | CONTEXT_SWITCH_DEFS__repeated_action_header_t {                                         |
 * |     |       |     .count = 3;                                                                         |
 * |     |       |     .last_executed = <last_action_executed_in_repeated>;                                |
 * |     |       |     .sub_action_type = CONTEXT_SWITCH_DEFS__ACTION_TYPE_ENABLE_LCU_DEFAULT;             |
 * |     |       | }                                                                                       |
 * |     |       | CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t {                                 |
 * |     |       |     .packed_lcu_id=<some_lcu_id>;                                                       |
 * |     |       |     .network_index=<some_network_index>                                                 |
 * |     |       |  }                                                                                      |
 * |     |       | CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t {                                 |
 * |     |       |     .packed_lcu_id=<some_lcu_id>;                                                       |
 * |     |       |     .network_index=<some_network_index>                                                 |
 * |     |       |  }                                                                                      |
 * |     |       | CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t {                                 |
 * |     |       |     .packed_lcu_id=<some_lcu_id>;                                                       |
 * |     |       |     .network_index=<some_network_index>                                                 |
 * |     V       |  }                                                                                      |
 * |    ...      | (Next action starting with CONTEXT_SWITCH_DEFS__common_action_header_t)                 |
 * |-------------------------------------------------------------------------------------------------------|
 * See also: "CONTROL_PROTOCOL__REPEATED_ACTION_t" in "control_protocol.h"
 */
typedef struct {
    uint8_t count;
    uint8_t last_executed;
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_t sub_action_type;
} CONTEXT_SWITCH_DEFS__repeated_action_header_t;

typedef struct {
    uint16_t descriptors_count;
    uint8_t cfg_channel_number;
} CONTEXT_SWITCH_DEFS__read_vdma_action_data_t;

typedef struct {
    uint16_t ccw_bursts;
    uint8_t cfg_channel_number;
} CONTEXT_SWITCH_DEFS__fetch_ccw_bursts_action_data_t;

typedef struct {
    uint8_t cluster_index;
    CONTORL_PROTOCOL__sequencer_config_t sequencer_config;
} CONTEXT_SWITCH_DEFS__trigger_sequencer_action_data_t;

typedef struct {
    uint8_t packed_lcu_id;
    uint8_t network_index;
    uint16_t kernel_done_address;
    uint32_t kernel_done_count;
} CONTEXT_SWITCH_DEFS__enable_lcu_action_non_default_data_t;

/* Default action - kernel_done_address and kernel_done_count has default values */
typedef struct {
    uint8_t packed_lcu_id;
    uint8_t network_index;
} CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t;

typedef struct {
    uint8_t packed_lcu_id;
} CONTEXT_SWITCH_DEFS__disable_lcu_action_data_t;

typedef struct {
    uint8_t vdma_channel_index;
    uint8_t edge_layer_direction;
    bool is_inter_context;
} CONTEXT_SWITCH_DEFS__deactivate_vdma_channel_action_data_t;

typedef struct {
    uint8_t vdma_channel_index;
    uint8_t stream_index;
    uint8_t network_index;
    uint32_t frame_periph_size;
    uint8_t credit_type;
    uint16_t periph_bytes_per_buffer;
} CONTEXT_SWITCH_DEFS__fetch_data_action_data_t;

typedef struct {
    uint8_t vdma_channel_index;
    uint8_t stream_index;
    bool is_dummy_stream;
} CONTEXT_SWITCH_DEFS__change_vdma_to_stream_mapping_data_t;

typedef struct {
    uint8_t h2d_vdma_channel_index;
    uint8_t d2h_vdma_channel_index;
    uint8_t network_index;
    uint32_t descriptors_per_frame;
    uint16_t programmed_descriptors_count;
} CONTEXT_SWITCH_DEFS__add_ddr_pair_info_action_data_t;

/* wait for interrupt structs */
typedef struct {
    uint8_t packed_lcu_id;
} CONTEXT_SWITCH_DEFS__lcu_interrupt_data_t;

typedef struct {
    uint8_t vdma_channel_index;
} CONTEXT_SWITCH_DEFS__vdma_dataflow_interrupt_data_t;

typedef struct {
    uint8_t sequencer_index;
} CONTEXT_SWITCH_DEFS__sequencer_interrupt_data_t;

typedef struct {
    uint8_t aggregator_index;
    uint8_t pred_cluster_ob_index;
    uint8_t pred_cluster_ob_cluster_index;
    uint8_t pred_cluster_ob_interface;
    uint8_t succ_prepost_ob_index;
    uint8_t succ_prepost_ob_interface;
} CONTEXT_SWITCH_DEFS__wait_nms_idle_data_t;

typedef struct {
    uint8_t vdma_channel_index;
    uint8_t stream_index;
    bool is_inter_context;
} CONTEXT_SWITCH_DEFS__wait_dma_idle_data_t;

typedef struct {
    uint8_t module_index;
} CONTEXT_SWITCH_DEFS__module_config_done_interrupt_data_t;

typedef struct {
    uint8_t application_index;
} CONTEXT_SWITCH_DEFS__application_change_interrupt_data_t;

/* edge layers structs */
typedef struct {
    uint8_t stream_index;
    uint8_t vdma_channel_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    bool is_single_context_app;
} CONTEXT_SWITCH_DEFS__activate_boundary_input_data_t;

typedef struct {
    uint8_t stream_index;
    uint8_t vdma_channel_index;
    uint8_t network_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    uint64_t host_descriptors_base_address;
    uint16_t initial_host_available_descriptors;
    uint8_t desc_list_depth;
} CONTEXT_SWITCH_DEFS__activate_inter_context_input_data_t;

typedef struct {
    uint8_t stream_index;
    uint8_t vdma_channel_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    uint64_t host_descriptors_base_address;
    uint16_t initial_host_available_descriptors;
    uint8_t desc_list_depth;
    bool fw_managed_channel;
} CONTEXT_SWITCH_DEFS__activate_ddr_buffer_input_data_t;

typedef struct {
    uint8_t stream_index;
    uint8_t vdma_channel_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    uint32_t frame_credits_in_bytes;
    uint16_t desc_page_size;
} CONTEXT_SWITCH_DEFS__activate_boundary_output_data_t;

typedef struct {
    uint8_t stream_index;
    uint8_t vdma_channel_index;
    uint8_t network_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    // TODO: add this to CONTEXT_SWITCH_DEFS__stream_reg_info_t
    uint32_t frame_credits_in_bytes;
    uint64_t host_descriptors_base_address;
    uint16_t initial_host_available_descriptors;
    uint16_t desc_page_size;
    uint8_t desc_list_depth;
} CONTEXT_SWITCH_DEFS__activate_inter_context_output_data_t;

typedef struct {
    uint8_t stream_index;
    uint8_t vdma_channel_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    uint32_t frame_credits_in_bytes;
    uint64_t host_descriptors_base_address;
    uint16_t initial_host_available_descriptors;
    uint16_t desc_page_size;
    uint8_t desc_list_depth;
    bool fw_managed_channel;
} CONTEXT_SWITCH_DEFS__activate_ddr_buffer_output_data_t;

typedef struct {
    uint8_t channel_index;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
} CONTEXT_SWITCH_DEFS__activate_cfg_channel_t;

typedef struct {
    uint8_t channel_index;
} CONTEXT_SWITCH_DEFS__deactivate_cfg_channel_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* __CONTEXT_SWITCH_DEFS__ */
