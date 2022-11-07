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
        std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
        std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,
        HcpConfigActiveAppHolder &active_net_group_holder,
        hailo_power_mode_t power_mode, EventPtr network_group_activated_event)
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
        power_mode, std::move(network_group_activated_event), status);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return object;
}

HcpConfigActivatedNetworkGroup::HcpConfigActivatedNetworkGroup(
        Device &device,
        HcpConfigActiveAppHolder &active_net_group_holder,
        const std::string &network_group_name,
        const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
        std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,    
        hailo_power_mode_t power_mode,
        EventPtr &&network_group_activated_event,
        hailo_status &status) :
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
    m_active_net_group_holder.set(*this);

    status = activate_low_level_streams(CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to activate low level streams");
        return;
    }

    status = m_network_group_activated_event->signal();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to signal network activation event");
        return;
    }
}

HcpConfigActivatedNetworkGroup::~HcpConfigActivatedNetworkGroup()
{
    if (!m_is_active) {
        return;
    }
    m_active_net_group_holder.clear();

    if (nullptr == m_network_group_activated_event) {
        return;
    }

    m_network_group_activated_event->reset();

    for (auto &name_pair : m_input_streams) {
        const auto status = name_pair.second->flush();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to flush input stream {} with status {}", name_pair.first, status);
        }
    }

    const auto status = deactivate_low_level_streams();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to deactivate low level streams");
    }
}

const std::string &HcpConfigActivatedNetworkGroup::get_network_group_name() const
{
    return m_network_group_name;
}

} /* namespace hailort */
