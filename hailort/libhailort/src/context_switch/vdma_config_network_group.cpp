#include "tracer_macros.hpp"
#include "context_switch/multi_context/vdma_config_network_group.hpp"
#include "network_group_internal.hpp"
#include "eth_stream.hpp"
#include "pcie_stream.hpp"
#include "mipi_stream.hpp"
#include "vstream_internal.hpp"

namespace hailort
{

Expected<VdmaConfigNetworkGroup> VdmaConfigNetworkGroup::create(ActiveNetGroupHolder &active_net_group_holder,
        const ConfigureNetworkParams &config_params, 
        std::shared_ptr<ResourcesManager> resources_manager, const std::string &hef_hash,
        std::shared_ptr<NetworkGroupMetadata> network_group_metadata,
        std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops)
{
    auto status = HAILO_UNINITIALIZED;

    VdmaConfigNetworkGroup object(active_net_group_holder, config_params,
        std::move(resources_manager), hef_hash, *network_group_metadata, status, std::move(net_flow_ops));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigNetworkGroup::VdmaConfigNetworkGroup(ActiveNetGroupHolder &active_net_group_holder,
    const ConfigureNetworkParams &config_params,
    std::shared_ptr<ResourcesManager> &&resources_manager, const std::string &hef_hash,
    const NetworkGroupMetadata &network_group_metadata, hailo_status &status,
    std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops) :
        ConfiguredNetworkGroupBase(config_params,
            network_group_metadata, std::move(net_flow_ops), status),
        m_active_net_group_holder(active_net_group_holder),
        m_resources_manager(std::move(resources_manager)),
        m_hef_hash(hef_hash)
{}

hailo_status VdmaConfigNetworkGroup::activate_impl(uint16_t dynamic_batch_size)
{
    auto status = HAILO_UNINITIALIZED;

    // Check that no network is currently activated
    CHECK(!m_active_net_group_holder.is_any_active(), HAILO_INTERNAL_FAILURE,
        "Cant activate network because a network is already activated");

    m_active_net_group_holder.set(*this);

    status = m_resources_manager->register_fw_managed_vdma_channels();
    CHECK_SUCCESS(status, "Failed to start fw managed vdma channels.");

    status = m_resources_manager->set_inter_context_channels_dynamic_batch_size(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to set inter-context channels dynamic batch size.");

    status = m_resources_manager->enable_state_machine(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to activate state-machine");

    status = activate_low_level_streams(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to activate low level streams");

    status = m_network_group_activated_event->signal();
    CHECK_SUCCESS(status, "Failed to signal network activation event");

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::deactivate_impl()
{
    auto status = HAILO_UNINITIALIZED;

    // Check that network is currently activated
    CHECK(m_active_net_group_holder.is_any_active(), HAILO_INTERNAL_FAILURE,
        "Cant Deactivate network because no network is already activated");

    // Make sure the network group we are deactivating is this object
    auto config_network_group_ref = m_active_net_group_holder.get().value();
    CHECK(this == std::addressof(config_network_group_ref.get()), HAILO_INTERNAL_FAILURE,
        "Trying to deactivate different network goup");

    m_active_net_group_holder.clear();
    
    m_network_group_activated_event->reset();

    status = deactivate_low_level_streams();
    CHECK_SUCCESS(status, "Failed to deactivate low level streams");

    status = m_resources_manager->unregister_fw_managed_vdma_channels();
    CHECK_SUCCESS(status, "Failed to stop fw managed vdma channels");

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> VdmaConfigNetworkGroup::create_activated_network_group(
      const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size)
{
    auto start_time = std::chrono::steady_clock::now();
    auto activated_net_group = VdmaConfigActivatedNetworkGroup::create(
        m_active_net_group_holder, name(), m_resources_manager, network_group_params, dynamic_batch_size,
        m_input_streams, m_output_streams, m_network_group_activated_event, m_deactivation_time_accumulator, (*this));
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    CHECK_EXPECTED(activated_net_group);

    LOGGER__INFO("Activating {} took {} milliseconds. Note that the function is asynchronous and"
                 " thus the network is not fully activated yet.", name(), elapsed_time_ms);
    m_activation_time_accumulator->add_data_point(elapsed_time_ms);

    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr =
        make_unique_nothrow<VdmaConfigActivatedNetworkGroup>(activated_net_group.release());
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return activated_net_group_ptr;
}

Expected<hailo_stream_interface_t> VdmaConfigNetworkGroup::get_default_streams_interface()
{
    return m_resources_manager->get_default_streams_interface();
}

hailo_status VdmaConfigNetworkGroup::set_scheduler_timeout(const std::chrono::milliseconds &/*timeout*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's timeout is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaConfigNetworkGroup::set_scheduler_threshold(uint32_t /*threshold*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's threshold is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

Expected<std::shared_ptr<LatencyMetersMap>> VdmaConfigNetworkGroup::get_latency_meters()
{
    auto latency_meters = m_resources_manager->get_latency_meters();
    return make_shared_nothrow<LatencyMetersMap>(latency_meters);
}

Expected<std::shared_ptr<VdmaChannel>> VdmaConfigNetworkGroup::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    return m_resources_manager->get_boundary_vdma_channel_by_stream_name(stream_name);
}

} /* namespace hailort */
