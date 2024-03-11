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


hailo_status VdmaConfigCoreOp::cancel_pending_transfers()
{
    // Best effort
    auto status = HAILO_SUCCESS;
    auto deactivate_status = HAILO_UNINITIALIZED;
    for (const auto &name_pair : m_input_streams) {
        deactivate_status = name_pair.second->cancel_pending_transfers();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to cancel pending transfers for input stream {}", name_pair.first);
            status = deactivate_status;
        }
    }
    for (const auto &name_pair : m_output_streams) {
        deactivate_status = name_pair.second->cancel_pending_transfers();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to cancel pending transfers for output stream {}", name_pair.first);
            status = deactivate_status;
        }
    }

    return status;
}

hailo_status VdmaConfigCoreOp::activate_impl(uint16_t dynamic_batch_size)
{
    auto status = HAILO_UNINITIALIZED;
    auto start_time = std::chrono::steady_clock::now();

    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE != dynamic_batch_size) {
        CHECK(dynamic_batch_size <= get_smallest_configured_batch_size(get_config_params()),
            HAILO_INVALID_ARGUMENT, "Batch size given is {} although max is {}", dynamic_batch_size,
            get_smallest_configured_batch_size(get_config_params()));
    }

    status = m_resources_manager->enable_state_machine(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to activate state-machine");

    CHECK_SUCCESS(activate_host_resources(), "Failed to activate host resources");

    //TODO: HRT-13019 - Unite with the calculation in core_op.cpp
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    TRACE(ActivateCoreOpTrace, std::string(m_resources_manager->get_dev_id()), vdevice_core_op_handle(), elapsed_time_ms);

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_impl()
{
    auto start_time = std::chrono::steady_clock::now();

    auto status = deactivate_host_resources();
    CHECK_SUCCESS(status);

    status = m_resources_manager->reset_state_machine();
    CHECK_SUCCESS(status, "Failed to reset context switch state machine");

    // After the state machine has been reset the vdma channels are no longer active, so we
    // can cancel pending transfers, thus allowing vdma buffers linked to said transfers to be freed
    status = cancel_pending_transfers();
    CHECK_SUCCESS(status, "Failed to cancel pending transfers");

    //TODO: HRT-13019 - Unite with the calculation in core_op.cpp
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    TRACE(DeactivateCoreOpTrace, std::string(m_resources_manager->get_dev_id()), vdevice_core_op_handle(), elapsed_time_ms);

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::shutdown()
{
    hailo_status status = HAILO_SUCCESS; // Success oriented

    auto abort_status = abort_low_level_streams();
    if (HAILO_SUCCESS != abort_status) {
        LOGGER__ERROR("Failed abort low level streams {}", abort_status);
        status = abort_status;
    }

    // On VdmaConfigCoreOp, shutdown is the same as deactivate. In the future, we can release the resources inside
    // the resource manager and free space in the firmware SRAM
    auto deactivate_status = deactivate_impl();
    if (HAILO_SUCCESS != deactivate_status) {
        LOGGER__ERROR("Failed deactivate core op with status {}", deactivate_status);
        status = deactivate_status;
    }

    return status;
}

hailo_status VdmaConfigCoreOp::activate_host_resources()
{
    CHECK_SUCCESS(m_resources_manager->start_vdma_transfer_launcher(), "Failed to start vdma transfer launcher");
    CHECK_SUCCESS(m_resources_manager->start_vdma_interrupts_dispatcher(), "Failed to start vdma interrupts");
    CHECK_SUCCESS(activate_low_level_streams(), "Failed to activate low level streams");
    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_host_resources()
{
    CHECK_SUCCESS(deactivate_low_level_streams(), "Failed to deactivate low level streams");
    CHECK_SUCCESS(m_resources_manager->stop_vdma_interrupts_dispatcher(), "Failed to stop vdma interrupts");
    CHECK_SUCCESS(m_resources_manager->stop_vdma_transfer_launcher(), "Failed to stop vdma transfers pending launch");
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
