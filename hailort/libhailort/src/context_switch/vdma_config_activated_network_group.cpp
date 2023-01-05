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
    ActiveNetGroupHolder &active_net_group_holder,
    const std::string &network_group_name,
    std::shared_ptr<ResourcesManager> resources_manager,
    const hailo_activate_network_group_params_t &network_group_params,
    uint16_t dynamic_batch_size,
    std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
    std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,         
    EventPtr network_group_activated_event,
    AccumulatorPtr deactivation_time_accumulator,
    ConfiguredNetworkGroupBase &network_group)
{
    CHECK(!active_net_group_holder.is_any_active(), make_unexpected(HAILO_INVALID_OPERATION),
        "network group is currently active. You must deactivate before activating another network_group");

    CHECK_ARG_NOT_NULL_AS_EXPECTED(deactivation_time_accumulator);

    auto status = HAILO_UNINITIALIZED;
    VdmaConfigActivatedNetworkGroup object(network_group_name, network_group_params, dynamic_batch_size, input_streams, output_streams,
        std::move(resources_manager), active_net_group_holder, std::move(network_group_activated_event),
        deactivation_time_accumulator, network_group, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigActivatedNetworkGroup::VdmaConfigActivatedNetworkGroup(
        const std::string &network_group_name,
        const hailo_activate_network_group_params_t &network_group_params,
        uint16_t dynamic_batch_size,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        std::shared_ptr<ResourcesManager> &&resources_manager,
        ActiveNetGroupHolder &active_net_group_holder,
        EventPtr &&network_group_activated_event,
        AccumulatorPtr deactivation_time_accumulator,
        ConfiguredNetworkGroupBase &network_group,
        hailo_status &status) :
    ActivatedNetworkGroupBase(network_group_params, input_streams, output_streams,
                              std::move(network_group_activated_event), status),
    m_network_group_name(network_group_name),
    m_should_reset_network_group(true),
    m_active_net_group_holder(active_net_group_holder),
    m_resources_manager(std::move(resources_manager)),
    m_deactivation_time_accumulator(deactivation_time_accumulator),
    m_keep_nn_config_during_reset(false)
{
    // Validate ActivatedNetworkGroup status
    if (HAILO_SUCCESS != status) {
        return;
    }
    
    // We know network_group is a VdmaConfigNetworkGroup
    status = network_group.activate_impl(dynamic_batch_size);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Error activating network group");
        return;
    }
}

VdmaConfigActivatedNetworkGroup::VdmaConfigActivatedNetworkGroup(VdmaConfigActivatedNetworkGroup &&other) noexcept :
    ActivatedNetworkGroupBase(std::move(other)),
    m_network_group_name(std::move(other.m_network_group_name)),
    m_should_reset_network_group(std::exchange(other.m_should_reset_network_group, false)),
    m_active_net_group_holder(other.m_active_net_group_holder),
    m_resources_manager(std::move(other.m_resources_manager)),
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

    auto config_network_group_ref = m_active_net_group_holder.get();
    if (!config_network_group_ref.has_value()) {
        LOGGER__ERROR("Error getting configured network group");
        return;
    }

    auto vdma_config_network_group = config_network_group_ref.value();

    status = vdma_config_network_group.get().deactivate_impl();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed deactivating network group");
    }

    status = m_resources_manager->reset_state_machine(m_keep_nn_config_during_reset);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to reset context switch with status {}", status);
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
    return m_resources_manager->read_intermediate_buffer(key);
}

hailo_status VdmaConfigActivatedNetworkGroup::set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset)
{
    m_keep_nn_config_during_reset = keep_nn_config_during_reset;
    return HAILO_SUCCESS;
}

} /* namespace hailort */
