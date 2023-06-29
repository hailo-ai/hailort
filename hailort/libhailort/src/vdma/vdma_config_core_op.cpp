#include "utils/profiler/tracer_macros.hpp"
#include "vdma/vdma_config_core_op.hpp"
#include "network_group/network_group_internal.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"


namespace hailort
{

Expected<VdmaConfigCoreOp> VdmaConfigCoreOp::create(ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &config_params, 
        std::shared_ptr<ResourcesManager> resources_manager,
        std::shared_ptr<CoreOpMetadata> metadata)
{
    auto status = HAILO_UNINITIALIZED;

    VdmaConfigCoreOp object(active_core_op_holder, config_params,
        std::move(resources_manager), metadata, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigCoreOp::VdmaConfigCoreOp(ActiveCoreOpHolder &active_core_op_holder,
    const ConfigureNetworkParams &config_params,
    std::shared_ptr<ResourcesManager> &&resources_manager,
    std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status) :
        CoreOp(config_params, metadata, status),
        m_active_core_op_holder(active_core_op_holder),
        m_resources_manager(std::move(resources_manager))
{}

hailo_status VdmaConfigCoreOp::activate_impl(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    auto status = HAILO_UNINITIALIZED;

    // Check that no network is currently activated
    CHECK(!m_active_core_op_holder.is_any_active(), HAILO_INTERNAL_FAILURE,
        "Cant activate network because a network is already activated");

    m_active_core_op_holder.set(*this);

    status = m_resources_manager->set_dynamic_batch_size(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to set inter-context channels dynamic batch size.");

    status = m_resources_manager->enable_state_machine(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to activate state-machine");

    status = m_resources_manager->start_vdma_interrupts_dispatcher();
    CHECK_SUCCESS(status, "Failed to start vdma interrupts");

    // Low-level streams assume that the vdma channels are enabled (happens in `enable_state_machine`), and that
    // the interrupt dispatcher is running (so they can wait for interrupts).
    status = activate_low_level_streams(dynamic_batch_size, resume_pending_stream_transfers);
    CHECK_SUCCESS(status, "Failed to activate low level streams");

    status = m_core_op_activated_event->signal();
    CHECK_SUCCESS(status, "Failed to signal network activation event");

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_impl(bool keep_nn_config_during_reset)
{
    auto status = deactivate_host_resources();
    CHECK_SUCCESS(status);

    status = m_resources_manager->reset_state_machine(keep_nn_config_during_reset);
    CHECK_SUCCESS(status, "Failed to reset context switch state machine");

    // After the state machine has been reset the vdma channels are no longer active, so we
    // can cancel pending async transfers, thus allowing vdma buffers linked to said transfers to be freed
    status = m_resources_manager->cancel_pending_async_transfers();
    CHECK_SUCCESS(status, "Failed to cancel pending async transfers");

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_host_resources()
{
    // Check that network is currently activated
    CHECK(m_active_core_op_holder.is_any_active(), HAILO_INTERNAL_FAILURE,
        "Cant Deactivate network because no network is already activated");

    // Make sure the core op we are deactivating is this object
    auto active_core_op_ref = m_active_core_op_holder.get().value();
    CHECK(this == std::addressof(active_core_op_ref.get()), HAILO_INTERNAL_FAILURE,
        "Trying to deactivate different network goup");

    m_active_core_op_holder.clear();

    m_core_op_activated_event->reset();

    auto status = deactivate_low_level_streams();
    CHECK_SUCCESS(status, "Failed to deactivate low level streams");

    // After disabling the vdma interrupts, we may still get some interrupts. On HRT-9430 we need to clean them.
    status = m_resources_manager->stop_vdma_interrupts_dispatcher();
    CHECK_SUCCESS(status, "Failed to stop vdma interrupts");

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> VdmaConfigCoreOp::create_activated_network_group(
    const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size,
    bool resume_pending_stream_transfers)
{
    auto start_time = std::chrono::steady_clock::now();
    auto activated_net_group = VdmaConfigActivatedCoreOp::create(
        m_active_core_op_holder, name(), m_resources_manager, network_group_params, dynamic_batch_size,
        m_input_streams, m_output_streams, m_core_op_activated_event, m_deactivation_time_accumulator,
        resume_pending_stream_transfers, *this);
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    CHECK_EXPECTED(activated_net_group);

    LOGGER__INFO("Activating {} took {} milliseconds. Note that the function is asynchronous and"
                 " thus the network is not fully activated yet.", name(), elapsed_time_ms);
    m_activation_time_accumulator->add_data_point(elapsed_time_ms);

    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr =
        make_unique_nothrow<VdmaConfigActivatedCoreOp>(activated_net_group.release());
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return activated_net_group_ptr;
}

Expected<hailo_stream_interface_t> VdmaConfigCoreOp::get_default_streams_interface()
{
    return m_resources_manager->get_default_streams_interface();
}

bool VdmaConfigCoreOp::is_scheduled() const
{
    // Scheduler allowed only when working with VDevice and scheduler enabled.
    return false;
}

hailo_status VdmaConfigCoreOp::set_scheduler_timeout(const std::chrono::milliseconds &/*timeout*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's timeout is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaConfigCoreOp::set_scheduler_threshold(uint32_t /*threshold*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's threshold is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaConfigCoreOp::set_scheduler_priority(uint8_t /*priority*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's priority is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

Expected<std::shared_ptr<LatencyMetersMap>> VdmaConfigCoreOp::get_latency_meters()
{
    auto latency_meters = m_resources_manager->get_latency_meters();
    return make_shared_nothrow<LatencyMetersMap>(latency_meters);
}

Expected<vdma::BoundaryChannelPtr> VdmaConfigCoreOp::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    return m_resources_manager->get_boundary_vdma_channel_by_stream_name(stream_name);
}

Expected<HwInferResults> VdmaConfigCoreOp::run_hw_infer_estimator()
{
    return m_resources_manager->run_hw_only_infer();
}

} /* namespace hailort */
