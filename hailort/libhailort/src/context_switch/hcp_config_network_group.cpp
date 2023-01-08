#include "context_switch/single_context/hcp_config_network_group.hpp"
#include "network_group_internal.hpp"
#include "control.hpp"

#define OUTPUT_CHANNEL_INDEX_OFFSET (16)


namespace hailort
{

HcpConfigNetworkGroup::HcpConfigNetworkGroup(Device &device, ActiveNetGroupHolder &active_net_group_holder,
    std::vector<WriteMemoryInfo> &&config, const ConfigureNetworkParams &config_params, NetworkGroupMetadata &&network_group_metadata,
    hailo_status &status, std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops)
        : ConfiguredNetworkGroupBase(config_params, network_group_metadata, std::move(net_flow_ops), status),
    m_config(std::move(config)), m_active_net_group_holder(active_net_group_holder), m_device(device)
{}

Expected<std::unique_ptr<ActivatedNetworkGroup>> HcpConfigNetworkGroup::create_activated_network_group(
    const hailo_activate_network_group_params_t &network_group_params, uint16_t /* dynamic_batch_size */)
{
    auto start_time = std::chrono::steady_clock::now();

    auto activated_net_group = HcpConfigActivatedNetworkGroup::create(m_device, m_config, name(), network_group_params,
        m_input_streams, m_output_streams, m_active_net_group_holder, m_config_params.power_mode,
        m_network_group_activated_event, (*this));
    CHECK_EXPECTED(activated_net_group);

    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr = make_unique_nothrow<HcpConfigActivatedNetworkGroup>(activated_net_group.release());
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Activating {} took {} milliseconds. Note that the function is asynchronous and thus the network is not fully activated yet.", name(), elapsed_time_ms);

    return activated_net_group_ptr;
}

Expected<hailo_stream_interface_t> HcpConfigNetworkGroup::get_default_streams_interface()
{
    return m_device.get_default_streams_interface();
}

hailo_status HcpConfigNetworkGroup::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    (void) timeout;
    (void) network_name;
    return HAILO_INVALID_OPERATION;
}

hailo_status HcpConfigNetworkGroup::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    (void) threshold;
    (void) network_name;
    return HAILO_INVALID_OPERATION;
}

Expected<std::shared_ptr<LatencyMetersMap>> HcpConfigNetworkGroup::get_latency_meters()
{
    /* hcp does not support latnecy. return empty map */
    LatencyMetersMap empty_map; 
    return make_shared_nothrow<LatencyMetersMap>(empty_map);
}

Expected<std::shared_ptr<VdmaChannel>> HcpConfigNetworkGroup::get_boundary_vdma_channel_by_stream_name(
    const std::string &stream_name)
{
    LOGGER__ERROR("get_boundary_vdma_channel_by_stream_name function for stream name {} is not supported in hcp config manager", 
        stream_name);
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status HcpConfigNetworkGroup::activate_impl(uint16_t dynamic_batch_size)
{
    m_active_net_group_holder.set(*this);

    auto status = activate_low_level_streams(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed activating low level streams");

    status = m_network_group_activated_event->signal();
    CHECK_SUCCESS(status, "Failed to signal network activation event");

    return HAILO_SUCCESS;
}
hailo_status HcpConfigNetworkGroup::deactivate_impl()
{
    auto expected_config_network_ref = m_active_net_group_holder.get();
    CHECK(expected_config_network_ref.has_value(), HAILO_INTERNAL_FAILURE, "Error getting configured network group");

    const auto &config_network_group = expected_config_network_ref.value();
    // Make sure the network group we are deactivating is this object
    CHECK(this == std::addressof(config_network_group.get()), HAILO_INTERNAL_FAILURE,
        "Trying to deactivate different network goup");

    m_active_net_group_holder.clear();

    if (!m_network_group_activated_event) {
        return HAILO_SUCCESS;
    }

    m_network_group_activated_event->reset();

    for (auto &name_pair : m_input_streams) {
        const auto status = name_pair.second->flush();
        CHECK_SUCCESS(status, "Failed to flush input stream {}", name_pair.first);
    }

    auto status = deactivate_low_level_streams();
    CHECK_SUCCESS(status, "Failed deactivating low level streams");

    return HAILO_SUCCESS;
}

} /* namespace hailort */
