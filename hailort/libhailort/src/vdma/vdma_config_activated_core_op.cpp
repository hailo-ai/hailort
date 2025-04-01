/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_config_activated_core_op.cpp
 * @brief VdmaConfigActivatedCoreOp implementation
 **/

#include "vdma/vdma_config_activated_core_op.hpp"
#include "device_common/control.hpp"

#include <chrono>


namespace hailort
{

Expected<VdmaConfigActivatedCoreOp> VdmaConfigActivatedCoreOp::create(
    ActiveCoreOpHolder &active_core_op_holder,
    const std::string &core_op_name,
    std::shared_ptr<ResourcesManager> resources_manager,
    // hailo_activate_network_group_params_t is currently an empty holder, if anything will be added to it ,
    // it will require a check that these params will be relevant for this one core op only.
    const hailo_activate_network_group_params_t &network_group_params,
    uint16_t dynamic_batch_size,
    std::map<std::string, std::shared_ptr<InputStreamBase>> &input_streams,
    std::map<std::string, std::shared_ptr<OutputStreamBase>> &output_streams,
    EventPtr core_op_activated_event,
    AccumulatorPtr deactivation_time_accumulator,
    CoreOp &core_op)
{
    CHECK(!active_core_op_holder.is_any_active(), make_unexpected(HAILO_INVALID_OPERATION),
        "core-op is currently active. You must deactivate before activating another core-op");

    CHECK_ARG_NOT_NULL_AS_EXPECTED(deactivation_time_accumulator);

    auto status = HAILO_UNINITIALIZED;
    VdmaConfigActivatedCoreOp object(core_op_name, network_group_params, dynamic_batch_size, input_streams, output_streams,
        std::move(resources_manager), active_core_op_holder, std::move(core_op_activated_event),
        deactivation_time_accumulator, core_op, status);
    if (HAILO_STREAM_ABORT == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigActivatedCoreOp::VdmaConfigActivatedCoreOp(
        const std::string &core_op_name,
        const hailo_activate_network_group_params_t &network_group_params,
        uint16_t dynamic_batch_size,
        std::map<std::string, std::shared_ptr<InputStreamBase>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStreamBase>> &output_streams,
        std::shared_ptr<ResourcesManager> &&resources_manager,
        ActiveCoreOpHolder &active_core_op_holder,
        EventPtr &&core_op_activated_event,
        AccumulatorPtr deactivation_time_accumulator,
        CoreOp &core_op,
        hailo_status &status) :
    ActivatedCoreOp(network_group_params, input_streams, output_streams,
                              std::move(core_op_activated_event), status),
    m_core_op_name(core_op_name),
    m_should_reset_core_op(true),
    m_active_core_op_holder(active_core_op_holder),
    m_resources_manager(std::move(resources_manager)),
    m_deactivation_time_accumulator(deactivation_time_accumulator)
{
    // Validate ActivatedCoreOp status
    if (HAILO_SUCCESS != status) {
        return;
    }
    
    // We know core_op is a VdmaConfigCoreOp
    status = core_op.activate_impl(dynamic_batch_size);
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Core-op activation failed because it was aborted by user");
        return;
    }
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Error activating core-op");
        return;
    }
}

VdmaConfigActivatedCoreOp::VdmaConfigActivatedCoreOp(VdmaConfigActivatedCoreOp &&other) noexcept :
    ActivatedCoreOp(std::move(other)),
    m_core_op_name(std::move(other.m_core_op_name)),
    m_should_reset_core_op(std::exchange(other.m_should_reset_core_op, false)),
    m_active_core_op_holder(other.m_active_core_op_holder),
    m_resources_manager(std::move(other.m_resources_manager)),
    m_deactivation_time_accumulator(std::move(other.m_deactivation_time_accumulator))
{}

VdmaConfigActivatedCoreOp::~VdmaConfigActivatedCoreOp()
{
    if (!m_should_reset_core_op) {
        return;
    }

    auto status = HAILO_UNINITIALIZED;
    const auto start_time = std::chrono::steady_clock::now();

    auto core_op_ref = m_active_core_op_holder.get();
    if (!core_op_ref.has_value()) {
        LOGGER__ERROR("Error getting core-op (status {})", status);
        return;
    }

    auto vdma_config_core_op = core_op_ref.value();

    status = vdma_config_core_op.get().deactivate_impl();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed deactivating core-op (status {})", status);
    }

    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Deactivating took {} ms", elapsed_time_ms);
    m_deactivation_time_accumulator->add_data_point(elapsed_time_ms);
}

// TODO: add get_core_op_name() for better code readability?
const std::string &VdmaConfigActivatedCoreOp::get_network_group_name() const
{
    // network_group name is the same as core_op name in this case.
    // VdmaConfigActivatedCoreOp should be used only for single core ops network groups.
    return m_core_op_name;
}

Expected<Buffer> VdmaConfigActivatedCoreOp::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    return m_resources_manager->read_intermediate_buffer(key);
}

} /* namespace hailort */
