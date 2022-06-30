#include "context_switch/multi_context/vdma_config_network_group.hpp"
#include "network_group_internal.hpp"
#include "eth_stream.hpp"
#include "pcie_stream.hpp"
#include "mipi_stream.hpp"
#include "vdevice_stream.hpp"

namespace hailort
{

Expected<VdmaConfigNetworkGroup> VdmaConfigNetworkGroup::create(VdmaConfigActiveAppHolder &active_net_group_holder,
        const ConfigureNetworkParams &config_params, 
        std::vector<std::shared_ptr<ResourcesManager>> resources_managers,
        std::shared_ptr<NetworkGroupMetadata> network_group_metadata, NetworkGroupSchedulerWeakPtr network_group_scheduler)
{
    auto status = HAILO_UNINITIALIZED;

    VdmaConfigNetworkGroup object(active_net_group_holder, config_params,
        std::move(resources_managers), *network_group_metadata, network_group_scheduler, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigNetworkGroup::VdmaConfigNetworkGroup(VdmaConfigActiveAppHolder &active_net_group_holder,
    const ConfigureNetworkParams &config_params,
    std::vector<std::shared_ptr<ResourcesManager>> &&resources_managers,
    const NetworkGroupMetadata &network_group_metadata, NetworkGroupSchedulerWeakPtr network_group_scheduler, hailo_status &status) :
        ConfiguredNetworkGroupBase(config_params,
            resources_managers[0]->get_network_group_index(), // All ResourceManagers shares the same net_group_index
            network_group_metadata, !network_group_scheduler.expired(), status),
        m_active_net_group_holder(active_net_group_holder),
        m_resources_managers(std::move(resources_managers)),
        m_network_group_scheduler(network_group_scheduler),
        m_network_group_handle(INVALID_NETWORK_GROUP_HANDLE) {}

Expected<std::unique_ptr<ActivatedNetworkGroup>> VdmaConfigNetworkGroup::activate_impl(
      const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size)
{
    auto start_time = std::chrono::steady_clock::now();
    auto activated_net_group = VdmaConfigActivatedNetworkGroup::create(
        m_active_net_group_holder, get_network_group_name(), m_resources_managers, network_group_params, dynamic_batch_size,
        m_input_streams, m_output_streams, m_network_group_activated_event, m_deactivation_time_accumulator);
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    CHECK_EXPECTED(activated_net_group);

    LOGGER__INFO("Activating {} took {} milliseconds. Note that the function is asynchronous and"
                 " thus the network is not fully activated yet.", get_network_group_name(), elapsed_time_ms);
    m_activation_time_accumulator->add_data_point(elapsed_time_ms);

    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr =
        make_unique_nothrow<VdmaConfigActivatedNetworkGroup>(activated_net_group.release());
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);
    
    return activated_net_group_ptr;
}

Expected<hailo_stream_interface_t> VdmaConfigNetworkGroup::get_default_streams_interface()
{
    auto first_streams_interface = m_resources_managers[0]->get_default_streams_interface();
    CHECK_EXPECTED(first_streams_interface);
#ifndef NDEBUG
    // Check that all physicall devices has the same interface
    for (auto &resoucres_manager : m_resources_managers) {
        auto iface = resoucres_manager->get_default_streams_interface();
        CHECK_EXPECTED(iface);
        CHECK_AS_EXPECTED(iface.value() == first_streams_interface.value(), HAILO_INTERNAL_FAILURE,
            "Not all default stream interfaces are the same");
    }
#endif
    return first_streams_interface;
}

Expected<uint8_t> VdmaConfigNetworkGroup::get_boundary_channel_index(uint8_t stream_index,
    hailo_stream_direction_t direction, const std::string &layer_name)
{
    // All ResourceManagers shares the same metadata and channels info
    return m_resources_managers[0]->get_boundary_channel_index(stream_index, direction, layer_name);
}

hailo_status VdmaConfigNetworkGroup::create_vdevice_streams_from_config_params(network_group_handle_t network_group_handle)
{
    // TODO - HRT-6931 - raise error on this case 
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_resources_managers.size())) {
        LOGGER__WARNING("Latency measurement is not supported on more than 1 physical device.");
    }

    for (const auto &stream_parameters_pair : m_config_params.stream_params_by_name) {
        switch (stream_parameters_pair.second.direction) {
            case HAILO_H2D_STREAM:
                {
                    auto status = create_input_vdevice_stream_from_config_params(stream_parameters_pair.second,
                        stream_parameters_pair.first, network_group_handle);
                    CHECK_SUCCESS(status);
                }
                break;
            case HAILO_D2H_STREAM:
                {
                    auto status = create_output_vdevice_stream_from_config_params(stream_parameters_pair.second,
                        stream_parameters_pair.first, network_group_handle);
                    CHECK_SUCCESS(status);
                }
                break;
            default:
                LOGGER__ERROR("stream name {} direction is invalid.", stream_parameters_pair.first);
                return HAILO_INVALID_ARGUMENT;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::create_input_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, network_group_handle_t network_group_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    CHECK(HAILO_STREAM_INTERFACE_PCIE == stream_params.stream_interface, HAILO_INVALID_OPERATION,
        "Only PCIe streams are supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);
    auto input_stream = VDeviceInputStream::create(m_resources_managers, edge_layer.value(),
        stream_name, network_group_handle, m_network_group_activated_event,
        m_network_group_scheduler);
    CHECK_EXPECTED_AS_STATUS(input_stream);
    m_input_streams.insert(make_pair(stream_name, input_stream.release()));

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::create_output_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, network_group_handle_t network_group_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    CHECK(HAILO_STREAM_INTERFACE_PCIE == stream_params.stream_interface, HAILO_INVALID_OPERATION,
        "Only PCIe streams are supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);
    auto output_stream = VDeviceOutputStream::create(m_resources_managers, edge_layer.value(),
        stream_name, network_group_handle, m_network_group_activated_event,
        m_network_group_scheduler);
    CHECK_EXPECTED_AS_STATUS(output_stream);
    m_output_streams.insert(make_pair(stream_name, output_stream.release()));

    return HAILO_SUCCESS;
}

void VdmaConfigNetworkGroup::set_network_group_handle(network_group_handle_t handle)
{
    m_network_group_handle = handle;
}

hailo_status VdmaConfigNetworkGroup::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    CHECK(network_group_scheduler, HAILO_INVALID_OPERATION,
        "Cannot set scheduler timeout for network group {}, as it is configured on a vdevice which does not have scheduling enabled", get_network_group_name());
    if (network_name != HailoRTDefaults::get_network_name(get_network_group_name())) {
        CHECK(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting scheduler timeout for a specific network is currently not supported");
    }
    auto status = network_group_scheduler->set_timeout(m_network_group_handle, timeout, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    CHECK(network_group_scheduler, HAILO_INVALID_OPERATION,
        "Cannot set scheduler threshold for network group {}, as it is configured on a vdevice which does not have scheduling enabled", get_network_group_name());
    if (network_name != HailoRTDefaults::get_network_name(get_network_group_name())) {
        CHECK(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting scheduler threshold for a specific network is currently not supported");
    }
    auto status = network_group_scheduler->set_threshold(m_network_group_handle, threshold, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<LatencyMetersMap>> VdmaConfigNetworkGroup::get_latnecy_meters()
{
    auto latency_meters = m_resources_managers[0]->get_latnecy_meters();
    return make_shared_nothrow<LatencyMetersMap>(latency_meters);
}

Expected<std::shared_ptr<VdmaChannel>> VdmaConfigNetworkGroup::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    if (1 < m_resources_managers.size()) {
        LOGGER__ERROR("get_boundary_vdma_channel_by_stream_name function is not supported on more than 1 physical device.");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    return m_resources_managers[0]->get_boundary_vdma_channel_by_stream_name(stream_name);
}

} /* namespace hailort */
