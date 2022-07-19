/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_context_activatedN_network_group.cpp
 * @brief VdmaConfigActivatedNetworkGroup implementation
 **/

#include "context_switch/multi_context/vdma_config_activated_network_group.hpp"
#include "control.hpp"
#include <chrono>

namespace hailort
{

Expected<VdmaConfigActivatedNetworkGroup> VdmaConfigActivatedNetworkGroup::create(
    VdmaConfigActiveAppHolder &active_net_group_holder,
    const std::string &network_group_name,
    std::vector<std::shared_ptr<ResourcesManager>> resources_managers,
    const hailo_activate_network_group_params_t &network_group_params,
    uint16_t dynamic_batch_size,
    std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
    std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,         
    EventPtr network_group_activated_event,
    AccumulatorPtr deactivation_time_accumulator)
{
    CHECK(!active_net_group_holder.is_any_active(), make_unexpected(HAILO_INVALID_OPERATION),
        "network group is currently active. You must deactivate before activating another network_group");

    CHECK_ARG_NOT_NULL_AS_EXPECTED(deactivation_time_accumulator);

    auto status = HAILO_UNINITIALIZED;
    VdmaConfigActivatedNetworkGroup object(network_group_name, network_group_params, dynamic_batch_size, input_streams, output_streams,
        std::move(resources_managers), active_net_group_holder, std::move(network_group_activated_event),
        deactivation_time_accumulator, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigActivatedNetworkGroup::VdmaConfigActivatedNetworkGroup(
        const std::string &network_group_name,
        const hailo_activate_network_group_params_t &network_group_params,
        uint16_t dynamic_batch_size,
        std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
        std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,
        std::vector<std::shared_ptr<ResourcesManager>> &&resources_managers,
        VdmaConfigActiveAppHolder &active_net_group_holder,
        EventPtr &&network_group_activated_event,
        AccumulatorPtr deactivation_time_accumulator,
        hailo_status &status) :
    ActivatedNetworkGroupBase(network_group_params, dynamic_batch_size, input_streams,
                              output_streams, std::move(network_group_activated_event), status),
    m_network_group_name(network_group_name),
    m_should_reset_network_group(true),
    m_active_net_group_holder(active_net_group_holder),
    m_resources_managers(std::move(resources_managers)),
    m_ddr_send_threads(),
    m_ddr_recv_threads(),
    m_deactivation_time_accumulator(deactivation_time_accumulator),
    m_keep_nn_config_during_reset(false)
{
    // Validate ActivatedNetworkGroup status
    if (HAILO_SUCCESS != status) {
        return;
    }
    m_active_net_group_holder.set(*this);

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->register_fw_managed_vdma_channels();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to start fw managed vdma channels.");
            return;
        }

        status = resources_manager->set_inter_context_channels_dynamic_batch_size(dynamic_batch_size);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to set inter-context channels dynamic batch size.");
            return;
        }
    }

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->enable_state_machine(dynamic_batch_size);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to activate state-machine");
            return;
        }
    }
}

VdmaConfigActivatedNetworkGroup::VdmaConfigActivatedNetworkGroup(VdmaConfigActivatedNetworkGroup &&other) noexcept :
    ActivatedNetworkGroupBase(std::move(other)),
    m_network_group_name(std::move(other.m_network_group_name)),
    m_should_reset_network_group(std::exchange(other.m_should_reset_network_group, false)),
    m_active_net_group_holder(other.m_active_net_group_holder),
    m_resources_managers(std::move(other.m_resources_managers)),
    m_ddr_send_threads(std::move(other.m_ddr_send_threads)),
    m_ddr_recv_threads(std::move(other.m_ddr_recv_threads)),
    m_deactivation_time_accumulator(std::move(other.m_deactivation_time_accumulator)),
    m_keep_nn_config_during_reset(std::move(other.m_keep_nn_config_during_reset))
{}

VdmaConfigActivatedNetworkGroup::~VdmaConfigActivatedNetworkGroup()
{
    if (!m_should_reset_network_group) {
        return;
    }

    auto status = HAILO_UNINITIALIZED;
    const auto start_time = std::chrono::steady_clock::now();

    m_active_net_group_holder.clear();
    deactivate_resources();

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->reset_state_machine(m_keep_nn_config_during_reset);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to reset context switch status");
        }
    }

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->unregister_fw_managed_vdma_channels();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to stop fw managed vdma channels");
        }
    }

    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Deactivating took {} ms", elapsed_time_ms);
    m_deactivation_time_accumulator->add_data_point(elapsed_time_ms);
}

const std::string &VdmaConfigActivatedNetworkGroup::get_network_group_name() const
{
    return m_network_group_name;
}

Expected<Buffer> VdmaConfigActivatedNetworkGroup::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    CHECK_AS_EXPECTED(1 == m_resources_managers.size(), HAILO_INVALID_OPERATION,
        "'get_intermediate_buffer' function works only when working with 1 physical device. number of physical devices: {}",
        m_resources_managers.size());
    return m_resources_managers[0]->read_intermediate_buffer(key);
}

hailo_status VdmaConfigActivatedNetworkGroup::set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset)
{
    m_keep_nn_config_during_reset = keep_nn_config_during_reset;
    return HAILO_SUCCESS;
}

} /* namespace hailort */
