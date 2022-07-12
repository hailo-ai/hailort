#ifndef __CONTEXT_SWITCH_HPP__
#define __CONTEXT_SWITCH_HPP__

#include <hailo/hailort.h>
#include "common/utils.hpp"
#include "control_protocol.h"
#include "control_protocol.hpp"

namespace hailort
{

/**
 * Build context switch none trigger
 *
 */
CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_none_trigger();

/**
 * Build context switch input stream trigger
 *
 * @param[in]        stream_index - input stream index
 *
 */
CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_input_stream_trigger(uint8_t stream_index);

/**
 * Build context switch output stream trigger
 *
 * @param[in]        stream_index - output stream index
 *
 */
CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_output_stream_trigger(uint8_t stream_index);

/**
 * Build context switch lcu trigger
 *
 * @param[in]        cluster_index - cluster index
 * @param[in]        lcu_index - lcu index
 *
 */
CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_lcu_trigger(uint8_t cluster_index, uint8_t lcu_index);

/**
 * Build context switch nms trigger
 *
 * @param[in]        aggregator_index - ppu aggregator index running the nms transformation
 * @param[in]        pred_cluster_ob_index - index of the preceding output buffer connected to the ppu
 * @param[in]        pred_cluster_ob_cluster_index - index of the preceding cluster whose output buffer
 *                                                   is connected to the ppu
 * @param[in]        pred_cluster_ob_interface - the interface of the preceding output buffer
 * @param[in]        succ_prepost_ob_index - index of the succeeding output buffer connected to the ppu
 * @param[in]        succ_prepost_ob_interface - the interface of the succeeding output buffer
 *
 */
CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_nms_trigger(uint8_t aggregator_index,
    uint8_t pred_cluster_ob_index, uint8_t pred_cluster_ob_cluster_index, uint8_t pred_cluster_ob_interface,
    uint8_t succ_prepost_ob_index, uint8_t succ_prepost_ob_interface);

/**
 * Build context switch dma idle trigger
 *
 * @param[in]        stream_index - the dma checked as idle is connected to this stream
 *
 */
CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_dma_idle_trigger(uint8_t stream_index);

/**
 * Build context switch trigger group header 
 *
 * @param[in]        context_info - struct holding all the context info
 * @param[in/out]    trigger_group_data_current_offset - pointer to the trigger group header
 * @param[in]        trigger - pointer to the trigger to be added (it's contents will be copied)
 *
 */
hailo_status HEF_METADATA__add_trigger_to_trigger_group(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **trigger_group_data_current_offset,
    const CONTROL_PROTOCOL__TRIGGER_t *trigger);

/**
 * Build read vdma action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     descriptors_count - descriptors_count to fetch
 * @param[in]     cfg_channel_handle - index of the cfg channel (not the PCIe channel number!)
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_read_vdma_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset, 
    uint16_t descriptors_count,
    uint8_t cfg_channel_handle,
    bool is_repeated);

/**
 * Build add ccw bursts action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     ccw_bursts - ccw bursts to fetch
 * @param[in]     cfg_channel_handle - index of the cfg channel (not the PCIe channel number!)
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_ccw_bursts_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset, 
    uint16_t ccw_bursts,
    uint8_t cfg_channel_handle,
    bool is_repeated);

/**
 * Build repeated (header) action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     sub_action - sub action type
 * @param[in]     num_actions - number of actions in the repeated group
 */
hailo_status HEF_METADATA__add_repeated_header_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset, 
    CONTROL_PROTOCOL__ACTION_TYPE_t sub_action_type,
    uint8_t num_actions
);

/**
 * Build wait for sequencer action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     sequencer_index - sequencer index
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_wait_for_sequencer_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    uint8_t sequencer_index,
    bool is_repeated);

/**
 * Build fetch new data action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     shmifo_index - shmifo index
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_fetch_new_data_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset, 
    uint8_t shmifo_index,
    bool is_repeated);

/**
 * Build context switch trigger sequencer action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     cluster_index - cluster index
 * @param[in]     initial_l3_cut - initial l3 cut
 * @param[in]     initial_l3_offset - initial_l3_offset
 * @param[in]     active_apu - bit map of active APUs
 * @param[in]     active_ia - bit map of active input aligners
 * @param[in]     active_sc - bit map of active subclusters
 * @param[in]     active_l2 - bit map of active l2 cuts
 * @param[in]     l2_offset_0 - offsets of write start for each active L2 cut (for first 32 subclusters)
 * @param[in]     l2_offset_1 - offsets of write start for each active L2 cut (for last 32 subclusters)
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 * 
 */
hailo_status HEF_METADATA__add_enable_sequencer_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    uint8_t cluster_index,
    uint8_t initial_l3_cut,
    uint16_t initial_l3_offset,
    uint32_t active_apu, 
    uint32_t active_ia, 
    uint64_t active_sc,
    uint64_t active_l2, 
    uint64_t l2_offset_0, 
    uint64_t l2_offset_1,
    bool is_repeated);

/**
 * build non default enable lcu action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     cluster_index - cluster index
 * @param[in]     lcu_index - lcu_index
 * @param[in]     kernel_done_address - kernel done address
 * @param[in]     kernel_done_count - kernel done count
 * @param[in]     network_index - network index
 * @param[in]     batch_size - batch size
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_enable_lcu_non_default_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    uint8_t cluster_index,
    uint8_t lcu_index,
    uint16_t kernel_done_address, 
    uint32_t kernel_done_count,
    uint8_t network_index,
    bool is_repeated);

/**
 * build non default enable lcu action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     cluster_index - cluster index
 * @param[in]     lcu_index - lcu_index
 * @param[in]     network_index - network index
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_enable_lcu_default_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    uint8_t cluster_index,
    uint8_t lcu_index,
    uint8_t network_index,
    bool is_repeated);

/**
 * build disable lcu action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     cluster_index - cluster index
 * @param[in]     lcu_index - lcu_index
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_disable_lcu_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset, 
    uint8_t cluster_index,
    uint8_t lcu_index,
    bool is_repeated);

/**
 * build wait for module config done
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action - pointer to the action
 * @param[in]     module_index - module index
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_wait_for_module_config_done_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    uint8_t module_index,
    bool is_repeated);

/**
 * build edge layer - vdma network boundary
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    edge_layer_current_offset - pointer to the location of the edge layer struct
 * @param[in]     stream_index - stream index 
 * @param[in]     vdma_channel_index - channel index 
 * @param[in]     network_index - network index 
 * @param[in]     nn_stream_config
 * @param[in]     frame_credits_in_bytes - context credits in bytes
 * @param[in]     desc_page_size - desc page size in bytes
 *
 */
hailo_status HEF_METADATA__add_network_boundary_output_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index, 
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    uint32_t frame_credits_in_bytes,
    uint16_t desc_page_size);

/**
 * build edge layer - vdma intermediate buffer output
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    edge_layer_current_offset - pointer to the location of the edge layer struct 
 * @param[in]     stream_index - stream index 
 * @param[in]     vdma_channel_index - channel index
 * @param[in]     network_index - network index 
 * @param[in]     nn_stream_config
 * @param[in]     host_buffer_info - info about host buffer
 */
hailo_status HEF_METADATA__add_inter_context_output_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index, 
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

/**
 * build edge layer - vdma DDR buffer output
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    edge_layer_current_offset - pointer to the location of the edge layer struct 
 * @param[in]     stream_index - stream index 
 * @param[in]     vdma_channel_index - channel index
 * @param[in]     network_index - network index 
 * @param[in]     nn_stream_config
 * @param[in]     frame_credits_in_bytes - context credits in bytes
 * @param[in]     host_descriptors_base_address - host descritpors base address
 * @param[in]     desc_page_size - descriptor page_size in bytes
 * @param[in]     desc_list_depth - descriptor list depth
 * @param[in]     buffered_rows_count - amount of rows to buffer.
 *
 */
hailo_status HEF_METADATA__add_ddr_buffer_output_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index, 
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    uint32_t frame_credits_in_bytes,
    uint64_t host_descriptors_base_address,
    uint16_t desc_page_size,
    uint8_t desc_list_depth,
    uint32_t buffered_rows_count);

/**
 * build edge layer - vdma network boundary input
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    edge_layer_current_offset - pointer to the location of the edge layer struct 
 * @param[in]     stream_index - stream index 
 * @param[in]     vdma_channel_index - channel index
 * @param[in]     network_index - network index 
 * @param[in]     nn_stream_config
 * @param[in]     desc_page_size - desc page size in bytes
 * @param[in]     initial_credit_size - initial credit size, if 0 is set the firmware takes its default value.
 *
 */
hailo_status HEF_METADATA__add_network_boundary_input_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index, 
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    uint16_t desc_page_size,
    uint32_t initial_credit_size);

/**
 * build edge layer - vdma intermediate buffer input
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    edge_layer_current_offset - pointer to the location of the edge layer struct 
 * @param[in]     stream_index - stream index 
 * @param[in]     vdma_channel_index - channel index
 * @param[in]     network_index - network index 
 * @param[in]     nn_stream_config
 * @param[in]     host_buffer_info - info about host buffer
 * @param[in]     initial_credit_size - initial credit size, if 0 is set the firmware takes its default value.
 *
 */
hailo_status HEF_METADATA__add_inter_context_input_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index, 
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info,
    uint32_t initial_credit_size);

/**
 * build edge layer - vdma ddr buffer input
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    edge_layer_current_offset - pointer to the location of the edge layer struct 
 * @param[in]     stream_index - stream index 
 * @param[in]     vdma_channel_index - channel index
 * @param[in]     network_index - network index 
 * @param[in]     nn_stream_config
 * @param[in]     host_descriptors_base_address - host descritpors base address
 * @param[in]     desc_list_depth - descriptor list depth
 * @param[in]     initial_credit_size - initial credit size, if 0 is set the firmware takes its default value.
 */
hailo_status HEF_METADATA__add_ddr_buffer_input_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index, 
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    uint64_t host_descriptors_base_address,
    uint8_t desc_list_depth,
    uint32_t initial_credit_size);

/**
 * Build add ddr pair info action
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action_data_current_offset - pointer to the action
 * @param[in]     h2d_vdma_channel_index - DDR pair host to device channel index
 * @param[in]     d2h_vdma_channel_index - DDR pair device to host channel index
 * @param[in]     descriptors_per_frame - expected total descritors transfered (per one frame)
 * @param[in]     programmed_descriptors_count - total size of the programed descriptors list
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_ddr_pair_info(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    const uint8_t h2d_vdma_channel_index, 
    const uint8_t d2h_vdma_channel_index,
    const uint32_t descriptors_per_frame,
    const uint16_t programmed_descriptors_count,
    bool is_repeated);

/**
 * Build add ddr buffering start
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action_data_current_offset - pointer to the action
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__add_ddr_buffering_start(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    bool is_repeated);

/**
 * Build add burst credits task start
 *
 * @param[in]     context_info - struct holding all the context info
 * @param[out]    action_data_current_offset - pointer to the action
 * @param[in]     is_repeated - 'true' if the action is part of a "repeated sequence" (a group of consecutive actions
 *                              with the same type)
 *
 */
hailo_status HEF_METADATA__burst_credits_task_start(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        bool is_repeated);

} /* namespace hailort */

#endif /* __CONTEXT_SWITCH__ */
