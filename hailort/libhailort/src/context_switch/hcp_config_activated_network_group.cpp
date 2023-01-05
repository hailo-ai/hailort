/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hcp_config_activated_network_group.cpp
 * @brief HcpConfigActivatedNetworkGroup implementation
 **/

#include "context_switch/single_context/hcp_config_activated_network_group.hpp"
#include "control.hpp"

namespace hailort
{

Expected<HcpConfigActivatedNetworkGroup> HcpConfigActivatedNetworkGroup::create(Device &device, std::vector<WriteMemoryInfo> &config,
        const std::string &network_group_name,
        const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        ActiveNetGroupHolder &active_net_group_holder,
        hailo_power_mode_t power_mode, EventPtr network_group_activated_event,
        ConfiguredNetworkGroupBase &network_group)
{
    CHECK(!active_net_group_holder.is_any_active(), make_unexpected(HAILO_INVALID_OPERATION),
        "network group is currently active. You must deactivate before activating another network_group");

    // Close older dataflows
    auto status = Control::close_all_streams(device);
    CHECK_SUCCESS_AS_EXPECTED(status);

    // Reset nn_core before writing configurations
    status = device.reset(HAILO_RESET_DEVICE_MODE_NN_CORE);
    CHECK_SUCCESS_AS_EXPECTED(status);

    for (auto &m : config) {
        status = device.write_memory(m.address, MemoryView(m.data));
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    HcpConfigActivatedNetworkGroup object(device, active_net_group_holder, network_group_name, network_group_params, input_streams, output_streams,
        power_mode, std::move(network_group_activated_event), network_group, status);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return object;
}

HcpConfigActivatedNetworkGroup::HcpConfigActivatedNetworkGroup(
        Device &device,
        ActiveNetGroupHolder &active_net_group_holder,
        const std::string &network_group_name,
        const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,    
        hailo_power_mode_t power_mode,
        EventPtr &&network_group_activated_event,
        ConfiguredNetworkGroupBase &network_group, hailo_status &status) :
    ActivatedNetworkGroupBase(network_group_params, input_streams, output_streams,
                              std::move(network_group_activated_event), status),
    m_active_net_group_holder(active_net_group_holder),
    m_is_active(true),
    m_power_mode(power_mode),
    m_device(device),
    m_network_group_name(network_group_name)
{
    // Validate ActivatedNetworkGroup status
    if (HAILO_SUCCESS != status) {
        return;
    }
    status = network_group.activate_impl(CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to activate network group");
        return;
    }
}

HcpConfigActivatedNetworkGroup::~HcpConfigActivatedNetworkGroup()
{
    if (!m_is_active) {
        return;
    }

    auto expected_config_network_ref = m_active_net_group_holder.get();
    if (!expected_config_network_ref.has_value()) {
        LOGGER__ERROR("Error getting configured network group");
        return;
    }
    const auto &config_network_group = expected_config_network_ref.value();

    const auto status = config_network_group.get().deactivate_impl();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to deactivate network group");
    }
}

const std::string &HcpConfigActivatedNetworkGroup::get_network_group_name() const
{
    return m_network_group_name;
}

} /* namespace hailort */
