/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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


#define CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__VDMA_CHANNEL_INDEX_MASK (0x3f)
#define CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__ENGINE_INDEX_MASK (0xc0)
#define CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__ENGINE_INDEX_SHIFT (6)

#define CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__SET(dst, engine_index, vdma_channel_index) do { \
        (dst) = (uint8_t)((vdma_channel_index) | ((engine_index) << CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__ENGINE_INDEX_SHIFT));\
    } while (0)

#define CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__READ(src, engine_index, vdma_channel_index) do {\
        (engine_index) = ((src) & CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__ENGINE_INDEX_MASK) >> \
            CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__ENGINE_INDEX_SHIFT; \
        (vdma_channel_index) = ((src) & CONTEXT_SWITCH_DEFS__PACKED_VDMA_CHANNEL_ID__VDMA_CHANNEL_INDEX_MASK); \
    } while (0)

#define CONTEXT_SWITCH_DEFS__WRITE_ACTION_BY_TYPE_MAX_SIZE (4)

// TODO HRT-12512: Update variable when / If DDR has it's own CMA region
#define CONTEXT_SWITCH_DEFS__START_M4_MAPPED_DDR_ADDRESS (0x80000000)
#define CONTEXT_SWITCH_DEFS__END_M4_MAPPED_DDR_ADDRESS (0x90000000)
#define CONTEXT_SWITCH_DEFS__START_M4_MAPPED_DDR_ADDRESS_AFTER_LUT (0x50000000)
#define CONTEXT_SWITCH_DEFS__DDR_ADDRESS_MASK (0x0FFFFFFF)
#define CONTEXT_SWITCH_DEFS__INVALID_DDR_CONTEXTS_BUFFER_ADDRESS (0)


#pragma pack(push, 1)
typedef struct {
    uint16_t core_bytes_per_buffer;
    uint16_t core_buffers_per_frame;
    uint16_t periph_bytes_per_buffer;
    uint32_t periph_buffers_per_frame;
    uint16_t feature_padding_payload;
    uint32_t buffer_padding_payload;
    uint16_t buffer_padding;
    bool is_core_hw_padding_config_in_dfc;
} CONTEXT_SWITCH_DEFS__stream_reg_info_t;

#if defined(_MSC_VER)
typedef enum : uint8_t {
#else
typedef enum __attribute__((packed)) {
#endif
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_CFG_CHANNEL_DESCRIPTORS = 0,
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
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_WAIT_FOR_NMS,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_CCW_BURSTS,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_VALIDATE_VDMA_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_BURST_CREDITS_TASK_START,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_BURST_CREDITS_TASK_RESET,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_DDR_BUFFERING_RESET,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_OPEN_BOUNDARY_INPUT_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_OPEN_BOUNDARY_OUTPUT_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ENABLE_NMS,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_WRITE_DATA_BY_TYPE,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_SWITCH_LCU_BATCH,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_CHANGE_BOUNDARY_INPUT_BATCH,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_PAUSE_VDMA_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_RESUME_VDMA_CHANNEL,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_CACHE_INPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_CACHE_OUTPUT,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_WAIT_FOR_CACHE_UPDATED,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_SLEEP,
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_HALT,

    /* Must be last */
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_COUNT
} CONTEXT_SWITCH_DEFS__ACTION_TYPE_t;

typedef enum {
    CONTEXT_SWITCH_DEFS__EDGE_LAYER_DIRECTION_UNINITIALIZED = 0,
    CONTEXT_SWITCH_DEFS__EDGE_LAYER_DIRECTION_HOST_TO_DEVICE,
    CONTEXT_SWITCH_DEFS__EDGE_LAYER_DIRECTION_DEVICE_TO_HOST,
} CONTEXT_SWITCH_DEFS__EDGE_LAYER_DIRECTION_t;

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
    uint8_t packed_vdma_channel_id;
} CONTEXT_SWITCH_DEFS__fetch_cfg_channel_descriptors_action_data_t;

typedef struct {
    uint16_t ccw_bursts;
    uint8_t config_stream_index;
} CONTEXT_SWITCH_DEFS__fetch_ccw_bursts_action_data_t;

typedef struct {
    uint8_t initial_l3_cut;
    uint16_t initial_l3_offset;
    uint32_t active_apu;
    uint32_t active_ia;
    uint64_t active_sc;
    uint64_t active_l2;
    uint64_t l2_offset_0;
    uint64_t l2_offset_1;
} CONTEXT_SWITCH_DEFS__sequencer_config_t;

typedef struct {
    uint8_t cluster_index;
    CONTEXT_SWITCH_DEFS__sequencer_config_t sequencer_config;
} CONTEXT_SWITCH_DEFS__trigger_sequencer_action_data_t;

typedef struct {
    uint8_t packed_lcu_id;
    uint8_t network_index;
    uint16_t kernel_done_address;
    uint32_t kernel_done_count;
} CONTEXT_SWITCH_DEFS__enable_lcu_action_non_default_data_t;

/* Default action - kernel_done_address, kernel_done_count have default values */
typedef struct {
    uint8_t packed_lcu_id;
    uint8_t network_index;
} CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t;

typedef struct {
    uint8_t packed_lcu_id;
} CONTEXT_SWITCH_DEFS__disable_lcu_action_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t edge_layer_direction;
    bool check_host_empty_num_available;
    uint8_t host_buffer_type;  // CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t
    uint32_t initial_credit_size;
} CONTEXT_SWITCH_DEFS__deactivate_vdma_channel_action_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t edge_layer_direction;
    bool check_host_empty_num_available;
    uint8_t host_buffer_type;  // CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t
    uint32_t initial_credit_size;
} CONTEXT_SWITCH_DEFS__validate_vdma_channel_action_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t edge_layer_direction;
} CONTEXT_SWITCH_DEFS__pause_vdma_channel_action_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t edge_layer_direction;
} CONTEXT_SWITCH_DEFS__resume_vdma_channel_action_data_t;

typedef enum {
    CONTEXT_SWITCH_DEFS__CREDIT_TYPE_UNINITIALIZED = 0,
    CONTEXT_SWITCH_DEFS__CREDIT_IN_BYTES,
    CONTEXT_SWITCH_DEFS__CREDIT_IN_DESCRIPTORS,
} CONTEXT_SWITCH_DEFS__CREDIT_TYPE_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    uint8_t network_index;
    uint32_t frame_periph_size;
    uint8_t credit_type;  // CONTEXT_SWITCH_DEFS__CREDIT_TYPE_t
    uint8_t host_buffer_type;  // CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t, relevant only for descriptors credit.
} CONTEXT_SWITCH_DEFS__fetch_data_action_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    bool is_dummy_stream;
} CONTEXT_SWITCH_DEFS__change_vdma_to_stream_mapping_data_t;

typedef struct {
    uint8_t h2d_packed_vdma_channel_id;
    uint8_t d2h_packed_vdma_channel_id;
    uint8_t network_index;
    uint32_t descriptors_per_frame;
    uint16_t programmed_descriptors_count;
} CONTEXT_SWITCH_DEFS__add_ddr_pair_info_action_data_t;

/* wait for interrupt structs */
typedef struct {
    uint8_t packed_lcu_id;
} CONTEXT_SWITCH_DEFS__lcu_interrupt_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    uint8_t network_index;
    bool is_inter_context;
    uint8_t host_buffer_type;  // CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t
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
} CONTEXT_SWITCH_DEFS__wait_nms_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    bool is_inter_context;
    uint8_t host_buffer_type;  // CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t
} CONTEXT_SWITCH_DEFS__wait_dma_idle_data_t;

typedef struct {
    uint8_t module_index;
} CONTEXT_SWITCH_DEFS__module_config_done_interrupt_data_t;

/* edge layers structs */
typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint32_t initial_credit_size;
} CONTEXT_SWITCH_DEFS__activate_boundary_input_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    uint8_t network_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint32_t initial_credit_size;
} CONTEXT_SWITCH_DEFS__activate_inter_context_input_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint32_t initial_credit_size;
    uint8_t connected_d2h_packed_vdma_channel_id;
} CONTEXT_SWITCH_DEFS__activate_ddr_buffer_input_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    uint8_t network_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint32_t initial_credit_size;
} CONTEXT_SWITCH_DEFS__activate_cache_input_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    uint8_t network_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
} CONTEXT_SWITCH_DEFS__activate_boundary_output_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    uint8_t network_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
} CONTEXT_SWITCH_DEFS__activate_inter_context_output_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint32_t buffered_rows_count;
} CONTEXT_SWITCH_DEFS__activate_ddr_buffer_output_data_t;

typedef struct {
    CONTEXT_SWITCH_DEFS__stream_reg_info_t stream_reg_info;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint16_t batch_size;
    uint8_t packed_vdma_channel_id;
    uint8_t stream_index;
    uint8_t network_index;
} CONTEXT_SWITCH_DEFS__activate_cache_output_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
    uint8_t stream_index;
    uint8_t network_index;
    uint16_t periph_bytes_per_buffer;
    uint32_t frame_periph_size;
} CONTEXT_SWITCH_DEFS__open_boundary_input_channel_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
} CONTEXT_SWITCH_DEFS__open_boundary_output_channel_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t config_stream_index;
    CONTROL_PROTOCOL__host_buffer_info_t host_buffer_info;
} CONTEXT_SWITCH_DEFS__activate_cfg_channel_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
    uint8_t config_stream_index;
} CONTEXT_SWITCH_DEFS__deactivate_cfg_channel_t;

typedef struct {
    uint8_t nms_unit_index;
    uint8_t network_index;
    uint16_t number_of_classes;
    uint16_t burst_size;
    uint8_t division_factor;
} CONTEXT_SWITCH_DEFS__enable_nms_action_t;

typedef enum {
    WRITE_ACTION_TYPE_GENERAL = 0,
    WRITE_ACTION_TYPE_WRITE_BATCH = 1,

    /* Must be last */
    WRITE_ACTION_BY_TYPE_COUNT
} CONTEXT_SWITCH_DEFS__WRITE_ACTION_TYPE_t;

typedef struct {
    uint32_t address;
    uint8_t data_type; //CONTEXT_SWITCH_DEFS__WRITE_ACTION_TYPE_t
    uint32_t data;
    uint8_t shift;
    uint32_t mask;
    uint8_t network_index;
} CONTEXT_SWITCH_DEFS__write_data_by_type_action_t;

typedef struct {
    uint8_t packed_lcu_id;
    uint8_t network_index;
    uint32_t kernel_done_count;
} CONTEXT_SWITCH_DEFS__switch_lcu_batch_action_data_t;

typedef struct {
    uint8_t packed_vdma_channel_id;
} CONTEXT_SWITCH_DEFS__change_boundary_input_batch_t;

typedef struct {
    uint32_t sleep_time;
} CONTEXT_SWITCH_DEFS__sleep_action_data_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* __CONTEXT_SWITCH_DEFS__ */
