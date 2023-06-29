#include "eth/hcp_config_core_op.hpp"
#include "device_common/control.hpp"


#define OUTPUT_CHANNEL_INDEX_OFFSET (16)


namespace hailort
{

HcpConfigCoreOp::HcpConfigCoreOp(Device &device, ActiveCoreOpHolder &active_core_op_holder,
    std::vector<WriteMemoryInfo> &&config, const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> metadata,
    hailo_status &status)
        : CoreOp(config_params, metadata, status),
    m_config(std::move(config)), m_active_core_op_holder(active_core_op_holder), m_device(device)
{}

Expected<std::unique_ptr<ActivatedNetworkGroup>> HcpConfigCoreOp::create_activated_network_group(
    const hailo_activate_network_group_params_t &network_group_params, uint16_t /* dynamic_batch_size */,
    bool /* resume_pending_stream_transfers */)
{
    auto start_time = std::chrono::steady_clock::now();

    auto activated_net_group = HcpConfigActivatedCoreOp::create(m_device, m_config, name(), network_group_params,
        m_input_streams, m_output_streams, m_active_core_op_holder, m_config_params.power_mode,
        m_core_op_activated_event, (*this));
    CHECK_EXPECTED(activated_net_group);

    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr = make_unique_nothrow<HcpConfigActivatedCoreOp>(activated_net_group.release());
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Activating {} took {} milliseconds. Note that the function is asynchronous and thus the network is not fully activated yet.", name(), elapsed_time_ms);

    return activated_net_group_ptr;
}

Expected<hailo_stream_interface_t> HcpConfigCoreOp::get_default_streams_interface()
{
    return m_device.get_default_streams_interface();
}

bool HcpConfigCoreOp::is_scheduled() const
{
    // Scheduler not supported on HcpConfigCoreOp
    return false;
}

hailo_status HcpConfigCoreOp::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    (void) timeout;
    (void) network_name;
    return HAILO_INVALID_OPERATION;
}

hailo_status HcpConfigCoreOp::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    (void) threshold;
    (void) network_name;
    return HAILO_INVALID_OPERATION;
}

hailo_status HcpConfigCoreOp::set_scheduler_priority(uint8_t /*priority*/, const std::string &/*network_name*/)
{
    return HAILO_INVALID_OPERATION;
}

Expected<std::shared_ptr<LatencyMetersMap>> HcpConfigCoreOp::get_latency_meters()
{
    /* hcp does not support latnecy. return empty map */
    LatencyMetersMap empty_map; 
    return make_shared_nothrow<LatencyMetersMap>(empty_map);
}

Expected<vdma::BoundaryChannelPtr> HcpConfigCoreOp::get_boundary_vdma_channel_by_stream_name(
    const std::string &stream_name)
{
    LOGGER__ERROR("get_boundary_vdma_channel_by_stream_name function for stream name {} is not supported on ETH core-ops",
        stream_name);
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<HwInferResults> HcpConfigCoreOp::run_hw_infer_estimator()
{
    LOGGER__ERROR("run_hw_infer_estimator function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status HcpConfigCoreOp::activate_impl(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    m_active_core_op_holder.set(*this);

    auto status = activate_low_level_streams(dynamic_batch_size, resume_pending_stream_transfers);
    CHECK_SUCCESS(status, "Failed activating low level streams");

    status = m_core_op_activated_event->signal();
    CHECK_SUCCESS(status, "Failed to signal network activation event");

    return HAILO_SUCCESS;
}
hailo_status HcpConfigCoreOp::deactivate_impl(bool /* keep_nn_config_during_reset */)
{
    auto expected_core_op_ref = m_active_core_op_holder.get();
    CHECK(expected_core_op_ref.has_value(), HAILO_INTERNAL_FAILURE, "Error getting configured core-op");

    const auto &core_op = expected_core_op_ref.value();
    // Make sure the core-op we are deactivating is this object
    CHECK(this == std::addressof(core_op.get()), HAILO_INTERNAL_FAILURE,
        "Trying to deactivate different core-op");

    m_active_core_op_holder.clear();

    if (!m_core_op_activated_event) {
        return HAILO_SUCCESS;
    }

    m_core_op_activated_event->reset();

    for (auto &name_pair : m_input_streams) {
        const auto status = name_pair.second->flush();
        CHECK_SUCCESS(status, "Failed to flush input stream {}", name_pair.first);
    }

    auto status = deactivate_low_level_streams();
    CHECK_SUCCESS(status, "Failed deactivating low level streams");

    return HAILO_SUCCESS;
}

} /* namespace hailort */
