#include "context_switch/single_context/hcp_config_network_group.hpp"
#include "network_group_internal.hpp"
#include "control.hpp"

#define OUTPUT_CHANNEL_INDEX_OFFSET (16)


namespace hailort
{

HcpConfigNetworkGroup::HcpConfigNetworkGroup(Device &device, HcpConfigActiveAppHolder &active_net_group_holder,
    std::vector<WriteMemoryInfo> &&config, const ConfigureNetworkParams &config_params, uint8_t net_group_index, 
    NetworkGroupMetadata &&network_group_metadata, hailo_status &status)
        : ConfiguredNetworkGroupBase(config_params, net_group_index, network_group_metadata, status),
          m_config(std::move(config)), m_active_net_group_holder(active_net_group_holder), m_device(device)
{}

Expected<std::unique_ptr<ActivatedNetworkGroup>> HcpConfigNetworkGroup::activate(
    const hailo_activate_network_group_params_t &network_group_params)
{
    auto start_time = std::chrono::steady_clock::now();

    auto activated_net_group = HcpConfigActivatedNetworkGroup::create(m_device, m_config, network_group_params,
        m_input_streams, m_output_streams, m_active_net_group_holder, m_config_params.power_mode,
        m_network_group_activated_event);
    CHECK_EXPECTED(activated_net_group);

    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr = make_unique_nothrow<HcpConfigActivatedNetworkGroup>(activated_net_group.release());
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Activating {} took {} milliseconds. Note that the function is asynchronous and thus the network is not fully activated yet.", get_network_group_name(), elapsed_time_ms);

    return activated_net_group_ptr;
}

Expected<hailo_stream_interface_t> HcpConfigNetworkGroup::get_default_streams_interface()
{
    return m_device.get_default_streams_interface();
}

Expected<uint8_t> HcpConfigNetworkGroup::get_boundary_channel_index(uint8_t stream_index,
    hailo_stream_direction_t direction, const std::string &/* layer_name */)
{
    return (direction == HAILO_H2D_STREAM) ? stream_index :
        static_cast<uint8_t>(stream_index + OUTPUT_CHANNEL_INDEX_OFFSET);
}

} /* namespace hailort */
