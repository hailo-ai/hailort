/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file download_action_list_command.hpp
 * @brief Download action list command
 **/

#ifndef _HAILO_DOWNLOAD_ACTION_LIST_COMMAND_HPP_
#define _HAILO_DOWNLOAD_ACTION_LIST_COMMAND_HPP_

#include "hailortcli.hpp"
#include "common.hpp"
#include "command.hpp"

#include "context_switch_defs.h"
#include "common/utils.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

class DownloadActionListCommand : public DeviceCommand
{
public:
    using DeviceCommand::execute;
    explicit DownloadActionListCommand(CLI::App &parent_app);
    // To be used from external commands
    static hailo_status execute(Device &device, const std::string &output_file_path,
        const ConfiguredNetworkGroupVector &network_groups={}, const std::string &hef_file_path="");
    static hailo_status execute(Device &device, std::shared_ptr<ConfiguredNetworkGroup> network_group,
        uint16_t batch_size, ordered_json &action_list_json, double fps, uint32_t network_group_index);
    static hailo_status write_to_json(ordered_json &action_list_json_param, const std::string &output_file_path);
    static Expected<ordered_json> init_json_object(Device &device, const std::string &hef_file_path);
    static hailo_status set_batch_to_measure(Device &device, uint16_t batch_to_measure);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_output_file_path;
    static constexpr int DEFAULT_JSON_TAB_WIDTH = 4;
    static constexpr int INVALID_NUMERIC_VALUE = -1;
    static std::string ACTION_LIST_FORMAT_VERSION() { return "2.0"; }

    static Expected<ordered_json> parse_hef_metadata(const std::string &hef_file_path);
    static bool is_valid_hef(const std::string &hef_file_path);
    static Expected<std::string> calc_md5_hexdigest(const std::string &hef_file_path);
    static hailo_status write_json(const ordered_json &json_obj, const std::string &output_file_path,
        int tab_width = DEFAULT_JSON_TAB_WIDTH);
    static Expected<ordered_json> parse_action_data(uint32_t base_address, uint8_t *action,
        uint32_t current_buffer_offset, uint32_t *action_length, CONTEXT_SWITCH_DEFS__ACTION_TYPE_t action_type,
        uint32_t timestamp, uint8_t sub_action_index = 0, bool sub_action_index_set = false,
        bool *is_repeated = nullptr, uint8_t *num_repeated = nullptr,
        CONTEXT_SWITCH_DEFS__ACTION_TYPE_t *sub_action_type = nullptr);
    static Expected<ordered_json> parse_single_repeated_action(uint32_t base_address, uint8_t *action,
        uint32_t current_buffer_offset, uint32_t *action_length, CONTEXT_SWITCH_DEFS__ACTION_TYPE_t action_type,
        uint32_t timestamp, uint8_t index_in_repeated_block);
    static Expected<ordered_json> parse_single_action(uint32_t base_address, uint8_t *context_action_list,
        uint32_t current_buffer_offset, uint32_t *action_length, bool *is_repeated, uint8_t *num_repeated,
        CONTEXT_SWITCH_DEFS__ACTION_TYPE_t *sub_action_type, uint32_t *time_stamp);
    static Expected<ordered_json> parse_context(Device &device, uint32_t network_group_id,
        CONTROL_PROTOCOL__context_switch_context_type_t context_type, uint16_t context_index,
        const std::string &context_name);
    static double get_accumulator_mean_value(const AccumulatorPtr &accumulator, double default_value = INVALID_NUMERIC_VALUE);
    static Expected<ordered_json> parse_network_groups(Device &device, const ConfiguredNetworkGroupVector &network_groups);
    static Expected<ordered_json> parse_network_group(Device &device,
        const std::shared_ptr<ConfiguredNetworkGroup> network_group, uint32_t network_group_id);
};

// JSON serialization

static std::pair<CONTEXT_SWITCH_DEFS__ACTION_TYPE_t, std::string> mapping[] = {
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_CFG_CHANNEL_DESCRIPTORS, "fetch_cfg_channel_descriptors"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_TRIGGER_SEQUENCER, "trigger_sequencer"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_DATA_FROM_VDMA_CHANNEL, "fetch_data_from_vdma_channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ENABLE_LCU_DEFAULT, "enable_lcu_default"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ENABLE_LCU_NON_DEFAULT, "enable_lcu_non_default"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_DISABLE_LCU, "disable_lcu"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_BOUNDARY_INPUT, "activate_boundary_input"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_BOUNDARY_OUTPUT, "activate_boundary_output"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_INTER_CONTEXT_INPUT, "activate_inter_context_input"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_INTER_CONTEXT_OUTPUT, "activate_inter_context_output"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_DDR_BUFFER_INPUT, "activate_ddr_buffer_input"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_DDR_BUFFER_OUTPUT, "activate_ddr_buffer_output"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_CACHE_INPUT, "activate_cache_input"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_CACHE_OUTPUT, "activate_cache_output"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_WAIT_FOR_CACHE_UPDATED, "wait_for_cache_updated"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_DEACTIVATE_VDMA_CHANNEL, "deactivate_vdma_channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_VALIDATE_VDMA_CHANNEL, "validate_vdma_channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_CHANGE_VDMA_TO_STREAM_MAPPING, "change_vdma_to_stream_mapping"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ADD_DDR_PAIR_INFO, "add_ddr_pair_info"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_DDR_BUFFERING_START, "ddr_buffering_start"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_BURST_CREDITS_TASK_START, "burst_credits_task_start"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_BURST_CREDITS_TASK_RESET, "burst_credits_task_reset"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_LCU_INTERRUPT, "lcu_interrupt"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_SEQUENCER_DONE_INTERRUPT, "sequencer_done_interrupt"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_INPUT_CHANNEL_TRANSFER_DONE_INTERRUPT, "input_channel_transfer_done_interrupt"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_OUTPUT_CHANNEL_TRANSFER_DONE_INTERRUPT, "output_channel_transfer_done_interrupt"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_MODULE_CONFIG_DONE_INTERRUPT, "module_config_done_interrupt"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_APPLICATION_CHANGE_INTERRUPT, "application_change_interrupt"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ACTIVATE_CFG_CHANNEL, "activate_cfg_channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_DEACTIVATE_CFG_CHANNEL, "deactivate_cfg_channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_REPEATED_ACTION, "repeated_action"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_WAIT_FOR_DMA_IDLE_ACTION, "wait_for_dma_idle_action"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_WAIT_FOR_NMS, "wait_for_nms_idle"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_FETCH_CCW_BURSTS, "fetch_ccw_bursts"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_DDR_BUFFERING_RESET, "ddr_buffering_reset"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_OPEN_BOUNDARY_INPUT_CHANNEL, "open_boundary_input_channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_OPEN_BOUNDARY_OUTPUT_CHANNEL, "open_boundary_output_channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_ENABLE_NMS, "enable_nms"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_WRITE_DATA_BY_TYPE, "write_data_by_type"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_SWITCH_LCU_BATCH, "switch_lcu_batch"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_CHANGE_BOUNDARY_INPUT_BATCH, "change boundary input batch"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_PAUSE_VDMA_CHANNEL, "pause vdma channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_RESUME_VDMA_CHANNEL, "resume vdma channel"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_SLEEP, "sleep"},
    {CONTEXT_SWITCH_DEFS__ACTION_TYPE_HALT, "halt"},
};
static_assert(ARRAY_ENTRIES(mapping) == CONTEXT_SWITCH_DEFS__ACTION_TYPE_COUNT,
    "Missing a mapping from a CONTEXT_SWITCH_DEFS__ACTION_TYPE_t to it's string value");

NLOHMANN_JSON_SERIALIZE_ENUM2(CONTEXT_SWITCH_DEFS__ACTION_TYPE_t, mapping);

// Default implementions
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__repeated_action_header_t, count, last_executed, sub_action_type);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__trigger_sequencer_action_data_t, cluster_index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__sequencer_interrupt_data_t, sequencer_index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__wait_nms_data_t, aggregator_index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__module_config_done_interrupt_data_t, module_index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__fetch_ccw_bursts_action_data_t, config_stream_index, ccw_bursts);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__enable_nms_action_t, nms_unit_index, network_index, number_of_classes, burst_size);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__write_data_by_type_action_t, address, data_type, data, shift, mask, network_index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CONTEXT_SWITCH_DEFS__sleep_action_data_t, sleep_time);

// Non-default implementations
void to_json(json &j, const CONTEXT_SWITCH_DEFS__deactivate_vdma_channel_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__validate_vdma_channel_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_boundary_input_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_inter_context_input_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_ddr_buffer_input_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_boundary_output_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_inter_context_output_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_ddr_buffer_output_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_cache_input_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_cache_output_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__enable_lcu_action_default_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__enable_lcu_action_non_default_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__disable_lcu_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__fetch_cfg_channel_descriptors_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__change_vdma_to_stream_mapping_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__fetch_data_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__wait_dma_idle_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__vdma_dataflow_interrupt_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__lcu_interrupt_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__activate_cfg_channel_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__deactivate_cfg_channel_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__add_ddr_pair_info_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__open_boundary_input_channel_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__open_boundary_output_channel_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__switch_lcu_batch_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__pause_vdma_channel_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__resume_vdma_channel_action_data_t &data);
void to_json(json &j, const CONTEXT_SWITCH_DEFS__change_boundary_input_batch_t &data);

#endif /* _HAILO_DOWNLOAD_ACTION_LIST_COMMAND_HPP_ */
