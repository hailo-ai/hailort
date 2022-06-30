#include <hailo/hailort.h>
#include "common/utils.hpp"

#include "context_switch/hef_metadata.hpp"
#include "control_protocol.h"
#include "context_switch_defs.h"
#include "byte_order.h"

#include <limits>

namespace hailort
{

CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_none_trigger()
{
    CONTROL_PROTOCOL__TRIGGER_t trigger{};
    trigger.type = CONTROL_PROTOCOL__CONTEXT_SWITCH_TRIGGER_TYPE_NONE;
    // Note: none_trigger is empty

    return trigger;
}

CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_input_stream_trigger(uint8_t stream_index)
{
    CONTROL_PROTOCOL__TRIGGER_t trigger{};
    trigger.type = CONTROL_PROTOCOL__CONTEXT_SWITCH_TRIGGER_TYPE_INPUT_STREAM;
    trigger.params.input_stream_trigger.stream_index = stream_index;

    return trigger;
}

CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_output_stream_trigger(uint8_t stream_index)
{
    CONTROL_PROTOCOL__TRIGGER_t trigger{};
    trigger.type = CONTROL_PROTOCOL__CONTEXT_SWITCH_TRIGGER_TYPE_OUTPUT_STREAM;
    trigger.params.output_stream_trigger.stream_index = stream_index;

    return trigger;
}

CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_lcu_trigger(uint8_t cluster_index, uint8_t lcu_index)
{
    CONTROL_PROTOCOL__TRIGGER_t trigger{};
    trigger.type = CONTROL_PROTOCOL__CONTEXT_SWITCH_TRIGGER_TYPE_LCU;
    trigger.params.lcu_trigger.cluster_index = cluster_index;
    trigger.params.lcu_trigger.lcu_index = lcu_index;

    return trigger;
}

CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_nms_trigger(uint8_t aggregator_index,
    uint8_t pred_cluster_ob_index, uint8_t pred_cluster_ob_cluster_index, uint8_t pred_cluster_ob_interface,
    uint8_t succ_prepost_ob_index, uint8_t succ_prepost_ob_interface)
{
    CONTROL_PROTOCOL__TRIGGER_t trigger{};
    trigger.type = CONTROL_PROTOCOL__CONTEXT_SWITCH_TRIGGER_TYPE_NMS_IDLE;
    trigger.params.nms_idle_trigger.aggregator_index = aggregator_index;
    trigger.params.nms_idle_trigger.pred_cluster_ob_index = pred_cluster_ob_index;
    trigger.params.nms_idle_trigger.pred_cluster_ob_cluster_index = pred_cluster_ob_cluster_index;
    trigger.params.nms_idle_trigger.pred_cluster_ob_interface = pred_cluster_ob_interface;
    trigger.params.nms_idle_trigger.succ_prepost_ob_index = succ_prepost_ob_index;
    trigger.params.nms_idle_trigger.succ_prepost_ob_interface = succ_prepost_ob_interface;

    return trigger;
}

CONTROL_PROTOCOL__TRIGGER_t HEF_METADATA__build_dma_idle_trigger(uint8_t stream_index)
{
    CONTROL_PROTOCOL__TRIGGER_t trigger{};
    trigger.type = CONTROL_PROTOCOL__CONTEXT_SWITCH_TRIGGER_TYPE_DMA_IDLE;
    trigger.params.dma_idle_trigger.stream_index = stream_index;

    return trigger;
}

uint16_t hef_metadata__return_network_data_offset(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info)
{
    uint8_t current_slice_index = 0;
    uint32_t current_offset_local = 0;

    current_offset_local = context_info->control_slicing_data.current_buillding_offset_inside_slice;
    current_slice_index = context_info->control_slicing_data.current_building_slice_index;

    if (0 < current_slice_index) {
        current_offset_local += context_info->control_slicing_data.control_slicing_offsets[current_slice_index - 1];
    }

    return ((uint16_t)(current_offset_local));
}

void hef_metadata__add_none_trigger_without_updating_slicing_info(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **trigger_group_data_current_offset)
{
    CONTROL_PROTOCOL__trigger_group_t trigger_group = {};

    trigger_group.trigger = HEF_METADATA__build_none_trigger();
    trigger_group.triggers_action_count = 0;

    context_info->control_slicing_data.current_building_trigger = 
        (CONTROL_PROTOCOL__trigger_group_t *)(*trigger_group_data_current_offset);

    memcpy((*trigger_group_data_current_offset), &(trigger_group), sizeof(trigger_group));
    *(trigger_group_data_current_offset) += sizeof(trigger_group);
}

hailo_status hef_metadata__update_slicing_info(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **struct_current_offset,
    uint8_t struct_size,
    bool is_action) 
{
    uint16_t current_building_slice_index = 0;
    bool would_exceed_slice_limit = false;
    bool would_exceed_max_trigger_action_count = false;

    CHECK(CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE >= static_cast<uint32_t>(struct_size),
        HAILO_CHUNK_TOO_LARGE);

    /* extract current slice */
    current_building_slice_index = context_info->control_slicing_data.current_building_slice_index;

    const auto last_trigger_index = (current_building_slice_index == 0) ?  0 : (current_building_slice_index - 1);
    const auto last_trigger_offset = context_info->control_slicing_data.control_slicing_offsets[last_trigger_index];
    const auto acummulated_metadata_offset = 
        last_trigger_offset + context_info->control_slicing_data.current_buillding_offset_inside_slice + struct_size; 

    if (acummulated_metadata_offset > CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_MAX_SIZE) {
        /* If condition failed - consider increasing 'CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_MAX_SIZE' define */
        LOGGER__ERROR("context metadata is larger than max data size. Acuumulated metadata offset is {}. Max size is {} ", 
            acummulated_metadata_offset, CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_MAX_SIZE);
        return HAILO_INTERNAL_FAILURE;
    }

    would_exceed_slice_limit = CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE 
            < (context_info->control_slicing_data.current_buillding_offset_inside_slice + struct_size);
    if (is_action) {
        /* context_info->control_slicing_data.current_building_trigger will be null if !is_action */
        static_assert(sizeof(uint16_t) == sizeof(context_info->control_slicing_data.current_building_trigger->triggers_action_count),
            "triggers_action_count field isn't uint16_t");
        would_exceed_max_trigger_action_count = context_info->control_slicing_data.current_building_trigger->triggers_action_count
            == (std::numeric_limits<uint16_t>::max() - 1);
    }

    /* If the next written struct would exceed the slice limit or max trigger action count */
    if (would_exceed_slice_limit || would_exceed_max_trigger_action_count) {
        /* Save slice end offset */
        context_info->control_slicing_data.control_slicing_offsets[current_building_slice_index] = 
            hef_metadata__return_network_data_offset(context_info);

        /* Advance slice index */
        (context_info->control_slicing_data.current_building_slice_index)++;
        current_building_slice_index++;
        context_info->control_slicing_data.current_buillding_offset_inside_slice = 0;

        /* If slice was inside action - add NONE trigger to start slice with trigger */
        if (is_action) {
            static_assert(CONTROL_PROTOCOL__CONTEXT_NETWORK_DATA_SINGLE_CONTROL_MAX_SIZE >= sizeof(CONTROL_PROTOCOL__trigger_group_t),
                "Trigger cant fit the slice size");

            /* Build trigger */
            hef_metadata__add_none_trigger_without_updating_slicing_info(context_info, struct_current_offset);

            /* Add the trigger offset */
            context_info->control_slicing_data.current_buillding_offset_inside_slice = sizeof(CONTROL_PROTOCOL__trigger_group_t);
            context_info->control_slicing_data.slice_triggers[current_building_slice_index]++;
        }
    }

    if (is_action) {
        context_info->control_slicing_data.current_building_trigger->triggers_action_count++;
    }

    /* Add the struct offset */
    context_info->control_slicing_data.current_buillding_offset_inside_slice = 
        (uint16_t)(context_info->control_slicing_data.current_buillding_offset_inside_slice + struct_size);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_trigger_to_trigger_group(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **trigger_group_data_current_offset,
        const CONTROL_PROTOCOL__TRIGGER_t *trigger)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__trigger_group_t trigger_group = {};
    uint8_t control_slice_index = 0; 

    CHECK_ARG_NOT_NULL(context_info);
    CHECK_ARG_NOT_NULL(trigger_group_data_current_offset);
    CHECK_ARG_NOT_NULL(*trigger_group_data_current_offset);
    CHECK_ARG_NOT_NULL(trigger);

    trigger_group.trigger = *trigger;
    trigger_group.triggers_action_count = 0;

    /* Update slice data (before actual write) */
    context_info->control_slicing_data.current_building_trigger = 
        (CONTROL_PROTOCOL__trigger_group_t *)(*trigger_group_data_current_offset);
    status = hef_metadata__update_slicing_info(context_info, 
            trigger_group_data_current_offset, 
            sizeof(trigger_group),
            false);
    CHECK_SUCCESS(status);
    control_slice_index = context_info->control_slicing_data.current_building_slice_index;
    
    /* Check for overflow */
    static_assert(sizeof(uint8_t) == sizeof(context_info->control_slicing_data.slice_triggers[control_slice_index]),
            "slice_triggers field isn't uint8_t");
    CHECK(context_info->control_slicing_data.slice_triggers[control_slice_index] < std::numeric_limits<uint8_t>::max(),
        HAILO_INTERNAL_FAILURE);
    context_info->control_slicing_data.slice_triggers[control_slice_index]++;

    memcpy((*trigger_group_data_current_offset), &(trigger_group), sizeof(trigger_group));
    *(trigger_group_data_current_offset) += sizeof(trigger_group);

    return HAILO_SUCCESS;
}

hailo_status hef_metadata__add_general_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        CONTROL_PROTOCOL__ACTION_TYPE_t action_type, 
        uint8_t action_data,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__ACTION_HEADER_t header{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    header.action_type = static_cast<uint8_t>(action_type);
    header.is_repeated = is_repeated;

    /* Update slice data (before actual write) */
    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(header) + sizeof(action_data),
            true);
    CHECK_SUCCESS(status);

    /* Setting action header */
    memcpy((*action_data_current_offset), &header, sizeof(header));
    *(action_data_current_offset) += sizeof(header);
    /* Setting action_data */
    memcpy((*action_data_current_offset), &action_data, sizeof(action_data));
    *(action_data_current_offset) += sizeof(action_data);

    return HAILO_SUCCESS;
}

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
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__TRIGGER_SEQUENCER_ACTION_t trigger_sequencer_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    trigger_sequencer_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_TRIGGER_SEQUENCER;
    trigger_sequencer_action.header.is_repeated = is_repeated;
    trigger_sequencer_action.cluster_index = cluster_index;
    trigger_sequencer_action.sequencer_config.initial_l3_cut = initial_l3_cut;
    trigger_sequencer_action.sequencer_config.initial_l3_offset = initial_l3_offset;
    trigger_sequencer_action.sequencer_config.active_apu = active_apu;
    trigger_sequencer_action.sequencer_config.active_ia = active_ia;
    trigger_sequencer_action.sequencer_config.active_sc = active_sc;
    trigger_sequencer_action.sequencer_config.active_l2 = active_l2;
    trigger_sequencer_action.sequencer_config.l2_offset_0 = l2_offset_0;
    trigger_sequencer_action.sequencer_config.l2_offset_1 = l2_offset_1;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(trigger_sequencer_action),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &trigger_sequencer_action, sizeof(trigger_sequencer_action));
    *(action_data_current_offset) += sizeof(trigger_sequencer_action);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_read_vdma_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        uint16_t descriptors_count,
        uint8_t cfg_channel_handle,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__READ_VDMA_ACTION_t read_vdma_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    read_vdma_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_READ_VDMA;
    read_vdma_action.header.is_repeated = is_repeated;
    read_vdma_action.descriptors_count = descriptors_count;
    read_vdma_action.cfg_channel_handle = cfg_channel_handle;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(read_vdma_action),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &read_vdma_action, sizeof(read_vdma_action));
    *(action_data_current_offset) += sizeof(read_vdma_action);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_ccw_bursts_action(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **action_data_current_offset,
    uint16_t ccw_bursts,
    uint8_t cfg_channel_handle,
    bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__FETCH_CCW_BURSTS_ACTION_t fetch_ccw_bursts{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    fetch_ccw_bursts.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_FETCH_CCW_BURSTS;
    fetch_ccw_bursts.header.is_repeated = is_repeated;
    fetch_ccw_bursts.ccw_bursts = ccw_bursts;
    fetch_ccw_bursts.cfg_channel_handle = cfg_channel_handle;

    status = hef_metadata__update_slicing_info(context_info, 
        action_data_current_offset, 
        sizeof(fetch_ccw_bursts),
        true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &fetch_ccw_bursts, sizeof(fetch_ccw_bursts));
    *(action_data_current_offset) += sizeof(fetch_ccw_bursts);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_repeated_header_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset, 
        CONTROL_PROTOCOL__ACTION_TYPE_t sub_action_type,
        uint8_t num_actions
)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__REPEATED_ACTION_t repeated_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    repeated_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ADD_REPEATED;
    repeated_action.header.is_repeated = false;
    repeated_action.sub_action_type = sub_action_type;
    repeated_action.num_actions = num_actions;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(repeated_action),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &repeated_action, sizeof(repeated_action));
    *(action_data_current_offset) += sizeof(repeated_action);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_wait_for_sequencer_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        uint8_t sequencer_index,
        bool is_repeated)
{
    return hef_metadata__add_general_action(
            context_info,
            action_data_current_offset,
            CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_WAIT_FOR_SEQUENCER_DONE,
            sequencer_index,
            is_repeated);
}

hailo_status HEF_METADATA__add_fetch_new_data_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset, 
        uint8_t shmifo_index,
        bool is_repeated)
{
    return hef_metadata__add_general_action(
            context_info,
            action_data_current_offset,
            CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_TRIGGER_NEW_DATA_FROM_DATA_INPUT,
            shmifo_index,
            is_repeated);
}

hailo_status HEF_METADATA__add_enable_lcu_non_default_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset, 
        uint8_t cluster_index,
        uint8_t lcu_index, 
        uint16_t kernel_done_address, 
        uint32_t kernel_done_count,
        uint8_t network_index,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__ENABLE_LCU_NON_DEAFULT_ACTION_t enable_lcu_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    enable_lcu_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ENABLE_LCU_NON_DEFAULT;
    enable_lcu_action.header.is_repeated = is_repeated;
    enable_lcu_action.cluster_index = cluster_index;
    enable_lcu_action.lcu_index = lcu_index;
    enable_lcu_action.kernel_done_address = kernel_done_address;
    enable_lcu_action.kernel_done_count = kernel_done_count;
    enable_lcu_action.network_index = network_index;

    status = hef_metadata__update_slicing_info(context_info,  action_data_current_offset,
        sizeof(enable_lcu_action), true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &enable_lcu_action, sizeof(enable_lcu_action));
    *(action_data_current_offset) += sizeof(enable_lcu_action);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_enable_lcu_default_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        uint8_t cluster_index,
        uint8_t lcu_index,
        uint8_t network_index,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__ENABLE_LCU_DEFAULT_ACTION_t enable_lcu_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    enable_lcu_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ENABLE_LCU_DEFAULT;
    enable_lcu_action.header.is_repeated = is_repeated;
    enable_lcu_action.cluster_index = cluster_index;
    enable_lcu_action.lcu_index = lcu_index;
    enable_lcu_action.network_index = network_index;

    status = hef_metadata__update_slicing_info(context_info,  action_data_current_offset,
        sizeof(enable_lcu_action), true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &enable_lcu_action, sizeof(enable_lcu_action));
    *(action_data_current_offset) += sizeof(enable_lcu_action);
   
    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_disable_lcu_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset, 
        uint8_t cluster_index,
        uint8_t lcu_index,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__DISABLE_LCU_ACTION_t disable_lcu_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    disable_lcu_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_DISABLE_LCU;
    disable_lcu_action.header.is_repeated = is_repeated;
    disable_lcu_action.cluster_index = cluster_index;
    disable_lcu_action.lcu_index = lcu_index;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(disable_lcu_action),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &disable_lcu_action, sizeof(disable_lcu_action));
    *(action_data_current_offset) += sizeof(disable_lcu_action);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_wait_for_module_config_done_action(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        uint8_t module_index,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTORL_PROTOCOL__WAIT_FOR_MODULE_CONFIG_DONE_ACTION_t wait_for_module_done_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    wait_for_module_done_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_WAIT_FOR_MODULE_CONFIG_DONE;
    wait_for_module_done_action.header.is_repeated = is_repeated;
    wait_for_module_done_action.module_index = module_index;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(wait_for_module_done_action),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &wait_for_module_done_action, sizeof(wait_for_module_done_action));
    *(action_data_current_offset) += sizeof(wait_for_module_done_action);

    return HAILO_SUCCESS;
}

/* build edge layers functions */
void hef_metadata__add_edge_layer_header(
    uint8_t **edge_layer_current_offset,
    CONTROL_PROTOCOL__EDGE_CONNECTION_TYPE_t edge_connection_type)
{
    CONTROL_PROTOCOL__edge_layer_header_t *edge_layer_header = nullptr;

    edge_layer_header = (CONTROL_PROTOCOL__edge_layer_header_t *)(*edge_layer_current_offset);
    edge_layer_header->edge_connection_type = static_cast<uint8_t>(edge_connection_type);
    *(edge_layer_current_offset) += sizeof(*edge_layer_header);
}

void hef_metadata__fill_edge_layer_common_info(
        CONTROL_PROTOCOL__edge_layer_common_info_t *edge_layer_common_info,
        uint8_t stream_index, 
        uint8_t vdma_channel_index,
        uint8_t network_index,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config)
{
    edge_layer_common_info->stream_index = stream_index;
    edge_layer_common_info->vdma_channel_index = vdma_channel_index;
    edge_layer_common_info->network_index = network_index;
    edge_layer_common_info->nn_stream_config.core_bytes_per_buffer = BYTE_ORDER__htons(nn_stream_config.core_bytes_per_buffer);
    edge_layer_common_info->nn_stream_config.core_buffers_per_frame = BYTE_ORDER__htons(nn_stream_config.core_buffers_per_frame);
    edge_layer_common_info->nn_stream_config.periph_bytes_per_buffer = BYTE_ORDER__htons(nn_stream_config.periph_bytes_per_buffer);
    edge_layer_common_info->nn_stream_config.feature_padding_payload = BYTE_ORDER__htons(nn_stream_config.feature_padding_payload);
    edge_layer_common_info->nn_stream_config.buffer_padding_payload = BYTE_ORDER__htons(nn_stream_config.buffer_padding_payload);
    edge_layer_common_info->nn_stream_config.buffer_padding = BYTE_ORDER__htons(nn_stream_config.buffer_padding);
}

hailo_status HEF_METADATA__add_network_boundary_output_edge_layer(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **edge_layer_current_offset,
        uint8_t stream_index, 
        uint8_t vdma_channel_index,
        uint8_t network_index,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        uint32_t frame_credits_in_bytes,
        uint16_t desc_page_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__network_boundary_output_t *edge_layer_info = nullptr;
    uint8_t control_slice_index = 0;

    CHECK_ARG_NOT_NULL(edge_layer_current_offset);
    CHECK_ARG_NOT_NULL(*edge_layer_current_offset);

    status = hef_metadata__update_slicing_info(context_info, 
            edge_layer_current_offset, 
            (sizeof(CONTROL_PROTOCOL__edge_layer_header_t ) + sizeof(*edge_layer_info)),
            false);
    CHECK_SUCCESS(status);
    control_slice_index = context_info->control_slicing_data.current_building_slice_index;  
    context_info->control_slicing_data.slice_edge_layers[control_slice_index]++;

    hef_metadata__add_edge_layer_header(edge_layer_current_offset,
        CONTROL_PROTOCOL__EDGE_CONNECTION_TYPE_NETWORK_BOUNDARY_OUTPUT);

    edge_layer_info = (CONTROL_PROTOCOL__network_boundary_output_t *)(*edge_layer_current_offset);

    hef_metadata__fill_edge_layer_common_info(&(edge_layer_info->common_info),
            stream_index, 
            vdma_channel_index,
            network_index,
            nn_stream_config);

    edge_layer_info->frame_credits_in_bytes = frame_credits_in_bytes;
    edge_layer_info->desc_page_size = desc_page_size;

    *(edge_layer_current_offset) += sizeof(*edge_layer_info);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_inter_context_output_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index,
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__inter_context_output_t *edge_layer_info = nullptr;
    uint8_t control_slice_index = 0;

    CHECK_ARG_NOT_NULL(edge_layer_current_offset);
    CHECK_ARG_NOT_NULL(*edge_layer_current_offset);

    status = hef_metadata__update_slicing_info(context_info, 
            edge_layer_current_offset, 
            (sizeof(CONTROL_PROTOCOL__edge_layer_header_t ) + sizeof(*edge_layer_info)),
            false);
    CHECK_SUCCESS(status);
    control_slice_index = context_info->control_slicing_data.current_building_slice_index;  
    context_info->control_slicing_data.slice_edge_layers[control_slice_index]++;

    hef_metadata__add_edge_layer_header(edge_layer_current_offset,
        CONTROL_PROTOCOL__EDGE_CONNECTION_TYPE_INTERMEDIATE_BUFFER_OUTPUT);

    edge_layer_info = (CONTROL_PROTOCOL__inter_context_output_t *)(*edge_layer_current_offset);

    hef_metadata__fill_edge_layer_common_info(&(edge_layer_info->common_info),
            stream_index, 
            vdma_channel_index,
            network_index,
            nn_stream_config);

    edge_layer_info->host_buffer_info = host_buffer_info;

    *(edge_layer_current_offset) += sizeof(*edge_layer_info);

    return HAILO_SUCCESS;
}

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
        uint32_t buffered_rows_count)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__ddr_buffer_output_t *edge_layer_info = nullptr;
    uint8_t control_slice_index = 0;

    CHECK_ARG_NOT_NULL(edge_layer_current_offset);
    CHECK_ARG_NOT_NULL(*edge_layer_current_offset);

    status = hef_metadata__update_slicing_info(context_info, 
            edge_layer_current_offset, 
            (sizeof(CONTROL_PROTOCOL__edge_layer_header_t ) + sizeof(*edge_layer_info)),
            false);
    CHECK_SUCCESS(status);
    control_slice_index = context_info->control_slicing_data.current_building_slice_index;  
    context_info->control_slicing_data.slice_edge_layers[control_slice_index]++;

    hef_metadata__add_edge_layer_header(edge_layer_current_offset,
        CONTROL_PROTOCOL__EDGE_CONNECTION_TYPE_DDR_BUFFER_OUTPUT);

    edge_layer_info = (CONTROL_PROTOCOL__ddr_buffer_output_t *)(*edge_layer_current_offset);

    hef_metadata__fill_edge_layer_common_info(&(edge_layer_info->common_info),
            stream_index, 
            vdma_channel_index,
            network_index,
            nn_stream_config);

    *(edge_layer_current_offset) += sizeof(*edge_layer_info);

    edge_layer_info->frame_credits_in_bytes = frame_credits_in_bytes;
    edge_layer_info->host_desc_address_info.host_descriptors_base_address = host_descriptors_base_address; 
    edge_layer_info->desc_page_size = desc_page_size;
    edge_layer_info->host_desc_address_info.desc_list_depth = desc_list_depth;
    edge_layer_info->buffered_rows_count = buffered_rows_count;

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_network_boundary_input_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index,
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    uint16_t desc_page_size,
    uint32_t initial_credit_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__network_boundary_input_t *edge_layer_info = nullptr;
    uint8_t control_slice_index = 0;

    CHECK_ARG_NOT_NULL(edge_layer_current_offset);
    CHECK_ARG_NOT_NULL(*edge_layer_current_offset);

    status = hef_metadata__update_slicing_info(context_info, 
            edge_layer_current_offset, 
            (sizeof(CONTROL_PROTOCOL__edge_layer_header_t ) + sizeof(*edge_layer_info)),
            false);
    CHECK_SUCCESS(status);
    control_slice_index = context_info->control_slicing_data.current_building_slice_index;  
    context_info->control_slicing_data.slice_edge_layers[control_slice_index]++;

    hef_metadata__add_edge_layer_header(edge_layer_current_offset,
        CONTROL_PROTOCOL__EDGE_CONNECTION_TYPE_NETWORK_BOUNDARY_INPUT);

    edge_layer_info = (CONTROL_PROTOCOL__network_boundary_input_t *)(*edge_layer_current_offset);

    hef_metadata__fill_edge_layer_common_info(&(edge_layer_info->common_info),
            stream_index, 
            vdma_channel_index,
            network_index,
            nn_stream_config);

    edge_layer_info->desc_page_size = desc_page_size;
    edge_layer_info->initial_credit_size = initial_credit_size;

    *(edge_layer_current_offset) += sizeof(*edge_layer_info);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_inter_context_input_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index,
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info,
    uint32_t initial_credit_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__inter_context_input_t *edge_layer_info = nullptr;
    uint8_t control_slice_index = 0;

    CHECK_ARG_NOT_NULL(edge_layer_current_offset);
    CHECK_ARG_NOT_NULL(*edge_layer_current_offset);

    status = hef_metadata__update_slicing_info(context_info, 
            edge_layer_current_offset, 
            (sizeof(CONTROL_PROTOCOL__edge_layer_header_t ) + sizeof(*edge_layer_info)),
            false);
    CHECK_SUCCESS(status);
    control_slice_index = context_info->control_slicing_data.current_building_slice_index;  
    context_info->control_slicing_data.slice_edge_layers[control_slice_index]++;

    hef_metadata__add_edge_layer_header(edge_layer_current_offset,
        CONTROL_PROTOCOL__EDGE_CONNECTION_TYPE_INTERMEDIATE_BUFFER_INPUT);

    edge_layer_info = (CONTROL_PROTOCOL__inter_context_input_t *)(*edge_layer_current_offset);

    hef_metadata__fill_edge_layer_common_info(&(edge_layer_info->common_info),
            stream_index, 
            vdma_channel_index,
            network_index,
            nn_stream_config);

    edge_layer_info->host_buffer_info = host_buffer_info;
    edge_layer_info->initial_credit_size = initial_credit_size;

    *(edge_layer_current_offset) += sizeof(*edge_layer_info);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_ddr_buffer_input_edge_layer(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **edge_layer_current_offset,
    uint8_t stream_index, 
    uint8_t vdma_channel_index,
    uint8_t network_index,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    uint64_t host_descriptors_base_address,
    uint8_t desc_list_depth,
    uint32_t initial_credit_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__ddr_buffer_input_t *edge_layer_info = nullptr;
    uint8_t control_slice_index = 0;

    CHECK_ARG_NOT_NULL(edge_layer_current_offset);
    CHECK_ARG_NOT_NULL(*edge_layer_current_offset);

    status = hef_metadata__update_slicing_info(context_info, 
            edge_layer_current_offset, 
            (sizeof(CONTROL_PROTOCOL__edge_layer_header_t ) + sizeof(*edge_layer_info)),
            false);
    CHECK_SUCCESS(status);
    control_slice_index = context_info->control_slicing_data.current_building_slice_index;  
    context_info->control_slicing_data.slice_edge_layers[control_slice_index]++;

    hef_metadata__add_edge_layer_header(edge_layer_current_offset,
        CONTROL_PROTOCOL__EDGE_CONNECTION_TYPE_DDR_BUFFER_INPUT);

    edge_layer_info = (CONTROL_PROTOCOL__ddr_buffer_input_t *)(*edge_layer_current_offset);

    hef_metadata__fill_edge_layer_common_info(&(edge_layer_info->common_info),
            stream_index, 
            vdma_channel_index,
            network_index,
            nn_stream_config);

    edge_layer_info->host_desc_address_info.host_descriptors_base_address = host_descriptors_base_address; 
    edge_layer_info->host_desc_address_info.desc_list_depth = desc_list_depth;
    edge_layer_info->initial_credit_size = initial_credit_size;

    *(edge_layer_current_offset) += sizeof(*edge_layer_info);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_ddr_pair_info(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        const uint8_t h2d_vdma_channel_index, 
        const uint8_t d2h_vdma_channel_index,
        const uint32_t descriptors_per_frame,
        const uint16_t programmed_descriptors_count,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__ADD_DDR_PAIR_ACTION_t ddr_pair_action{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    ddr_pair_action.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ADD_DDR_PAIR_INFO;
    ddr_pair_action.header.is_repeated = is_repeated;
    ddr_pair_action.h2d_vdma_channel_index = h2d_vdma_channel_index;
    ddr_pair_action.d2h_vdma_channel_index = d2h_vdma_channel_index;
    ddr_pair_action.descriptors_per_frame = descriptors_per_frame;
    ddr_pair_action.programmed_descriptors_count = programmed_descriptors_count;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(ddr_pair_action),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &ddr_pair_action, sizeof(ddr_pair_action));
    *(action_data_current_offset) += sizeof(ddr_pair_action);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__add_ddr_buffering_start(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__ADD_DDR_BUFFERING_START_ACTION_t ddr_buffering_start{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    ddr_buffering_start.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ADD_DDR_BUFFERING_START;
    ddr_buffering_start.header.is_repeated = is_repeated;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(ddr_buffering_start),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &ddr_buffering_start, sizeof(ddr_buffering_start));
    *(action_data_current_offset) += sizeof(ddr_buffering_start);

    return HAILO_SUCCESS;
}

hailo_status HEF_METADATA__burst_credits_task_start(
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **action_data_current_offset,
        bool is_repeated)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__BURST_CREDITS_TASK_START_ACTION_T burst_credits_task_start{};

    CHECK_ARG_NOT_NULL(action_data_current_offset);
    CHECK_ARG_NOT_NULL(*action_data_current_offset);

    burst_credits_task_start.header.action_type = CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_BURST_CREDITS_TASK_START;
    burst_credits_task_start.header.is_repeated = is_repeated;

    status = hef_metadata__update_slicing_info(context_info, 
            action_data_current_offset, 
            sizeof(burst_credits_task_start),
            true);
    CHECK_SUCCESS(status);

    memcpy((*action_data_current_offset), &burst_credits_task_start, sizeof(burst_credits_task_start));
    *(action_data_current_offset) += sizeof(burst_credits_task_start);

    return HAILO_SUCCESS;
}
/* End of context switch info build functions */

} /* namespace hailort */
