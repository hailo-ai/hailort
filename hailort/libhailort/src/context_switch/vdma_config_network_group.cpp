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
        m_network_group_scheduler(network_group_scheduler) {}

Expected<std::unique_ptr<ActivatedNetworkGroup>> VdmaConfigNetworkGroup::activate_impl(
      const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size)
{
    auto start_time = std::chrono::steady_clock::now();
    auto activated_net_group = VdmaConfigActivatedNetworkGroup::create(
        m_active_net_group_holder, m_resources_managers, network_group_params, dynamic_batch_size,
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

hailo_status VdmaConfigNetworkGroup::create_vdevice_streams_from_config_params()
{
    if ((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) {
        if (1 == m_resources_managers.size()) {
            // Best affort for starting latency meter.
            auto networks_names = m_network_group_metadata.get_network_names();
            for (auto &network_name : networks_names) {
                auto layer_infos = m_network_group_metadata.get_all_layer_infos(network_name);
                CHECK_EXPECTED_AS_STATUS(layer_infos);
                auto latency_meter = ConfiguredNetworkGroupBase::create_hw_latency_meter(m_resources_managers[0]->get_device(),
                    layer_infos.value());
                if (latency_meter) {
                    m_latency_meter.emplace(network_name, latency_meter.release());
                    LOGGER__DEBUG("Starting hw latency measurement for network {}", network_name);
                }
            }
        } else {
            LOGGER__WARNING("Latency measurement is not supported on more than 1 physical device.");
        }
    }

    for (const auto &stream_parameters_pair : m_config_params.stream_params_by_name) {
        switch (stream_parameters_pair.second.direction) {
            case HAILO_H2D_STREAM:
                {
                    auto status = create_input_vdevice_stream_from_config_params(stream_parameters_pair.second,
                        stream_parameters_pair.first);
                    CHECK_SUCCESS(status);
                }
                break;
            case HAILO_D2H_STREAM:
                {
                    auto status = create_output_vdevice_stream_from_config_params(stream_parameters_pair.second,
                        stream_parameters_pair.first);
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
    const std::string &stream_name)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    auto latency_meter = (contains(m_latency_meter, edge_layer->network_name)) ? m_latency_meter.at(edge_layer->network_name) : nullptr;

    CHECK(HAILO_STREAM_INTERFACE_PCIE == stream_params.stream_interface, HAILO_INVALID_OPERATION,
        "Only PCIe streams are supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);
    auto input_stream = VDeviceInputStream::create(m_resources_managers, edge_layer.value(),
        stream_name, get_network_group_name(), m_network_group_activated_event,
        m_network_group_scheduler, latency_meter);
    CHECK_EXPECTED_AS_STATUS(input_stream);
    m_input_streams.insert(make_pair(stream_name, input_stream.release()));

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::create_output_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    auto latency_meter = (contains(m_latency_meter, edge_layer->network_name)) ? m_latency_meter.at(edge_layer->network_name) : nullptr;

    CHECK(HAILO_STREAM_INTERFACE_PCIE == stream_params.stream_interface, HAILO_INVALID_OPERATION,
        "Only PCIe streams are supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);
    auto output_stream = VDeviceOutputStream::create(m_resources_managers, edge_layer.value(),
        stream_name, get_network_group_name(), m_network_group_activated_event,
        m_network_group_scheduler, latency_meter);
    CHECK_EXPECTED_AS_STATUS(output_stream);
    m_output_streams.insert(make_pair(stream_name, output_stream.release()));

    return HAILO_SUCCESS;
}

} /* namespace hailort */
