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
        CoreOp(config_params, metadata, active_core_op_holder, status),
        m_resources_manager(std::move(resources_manager))
{}

hailo_status VdmaConfigCoreOp::activate_impl(uint16_t dynamic_batch_size)
{
    auto status = HAILO_UNINITIALIZED;

    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE != dynamic_batch_size) {
        CHECK(dynamic_batch_size <= get_smallest_configured_batch_size(get_config_params()),
            HAILO_INVALID_ARGUMENT, "Batch size given is {} although max is {}", dynamic_batch_size,
            get_smallest_configured_batch_size(get_config_params()));
    }

    status = m_resources_manager->enable_state_machine(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to activate state-machine");

    status = m_resources_manager->start_vdma_interrupts_dispatcher();
    CHECK_SUCCESS(status, "Failed to start vdma interrupts");

    // Low-level streams assume that the vdma channels are enabled (happens in `enable_state_machine`), and that
    // the interrupt dispatcher is running (so they can wait for interrupts).
    status = activate_low_level_streams();
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Low level streams activation failed because some were aborted by user");
        return status;
    }
    CHECK_SUCCESS(status, "Failed to activate low level streams");

    TRACE(SwitchCoreOpTrace, std::string(m_resources_manager->get_dev_id()), vdevice_core_op_handle());

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_impl()
{
    auto status = deactivate_host_resources();
    CHECK_SUCCESS(status);

    status = m_resources_manager->reset_state_machine();
    CHECK_SUCCESS(status, "Failed to reset context switch state machine");

    // After the state machine has been reset the vdma channels are no longer active, so we
    // can cancel pending transfers, thus allowing vdma buffers linked to said transfers to be freed
    status = m_resources_manager->cancel_pending_transfers();
    CHECK_SUCCESS(status, "Failed to cancel pending transfers");

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_host_resources()
{
    auto status = deactivate_low_level_streams();
    CHECK_SUCCESS(status, "Failed to deactivate low level streams");

    // After disabling the vdma interrupts, we may still get some interrupts. On HRT-9430 we need to clean them.
    status = m_resources_manager->stop_vdma_interrupts_dispatcher();
    CHECK_SUCCESS(status, "Failed to stop vdma interrupts");

    return HAILO_SUCCESS;
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
    auto res = make_shared_nothrow<LatencyMetersMap>(latency_meters);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
}

Expected<vdma::BoundaryChannelPtr> VdmaConfigCoreOp::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    return m_resources_manager->get_boundary_vdma_channel_by_stream_name(stream_name);
}

Expected<HwInferResults> VdmaConfigCoreOp::run_hw_infer_estimator()
{
    return m_resources_manager->run_hw_only_infer();
}

Expected<Buffer> VdmaConfigCoreOp::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    return m_resources_manager->read_intermediate_buffer(key);
}

} /* namespace hailort */
