#include "context_switch/multi_context/vdma_config_network_group.hpp"
#include "network_group_internal.hpp"
#include "eth_stream.hpp"
#include "pcie_stream.hpp"
#include "mipi_stream.hpp"
#include "vdevice_stream.hpp"
#include "vdevice_stream_wrapper.hpp"
#include "vstream_internal.hpp"

namespace hailort
{

Expected<VdmaConfigNetworkGroup> VdmaConfigNetworkGroup::create(VdmaConfigActiveAppHolder &active_net_group_holder,
        const ConfigureNetworkParams &config_params, 
        std::vector<std::shared_ptr<ResourcesManager>> resources_managers, const std::string &hef_hash,
        std::shared_ptr<NetworkGroupMetadata> network_group_metadata, NetworkGroupSchedulerWeakPtr network_group_scheduler)
{
    auto status = HAILO_UNINITIALIZED;

    VdmaConfigNetworkGroup object(active_net_group_holder, config_params,
        std::move(resources_managers), hef_hash, *network_group_metadata, network_group_scheduler, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigNetworkGroup::VdmaConfigNetworkGroup(VdmaConfigActiveAppHolder &active_net_group_holder,
    const ConfigureNetworkParams &config_params,
    std::vector<std::shared_ptr<ResourcesManager>> &&resources_managers, const std::string &hef_hash,
    const NetworkGroupMetadata &network_group_metadata, NetworkGroupSchedulerWeakPtr network_group_scheduler, hailo_status &status) :
        ConfiguredNetworkGroupBase(config_params,
            resources_managers[0]->get_network_group_index(), // All ResourceManagers shares the same net_group_index
            network_group_metadata, !network_group_scheduler.expired(), status),
        m_active_net_group_holder(active_net_group_holder),
        m_resources_managers(std::move(resources_managers)),
        m_network_group_scheduler(network_group_scheduler),
        m_scheduler_handle(INVALID_NETWORK_GROUP_HANDLE),
        m_multiplexer_handle(0),
        m_hef_hash(hef_hash)
{}

Expected<std::unique_ptr<ActivatedNetworkGroup>> VdmaConfigNetworkGroup::activate_impl(
      const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size)
{
    auto start_time = std::chrono::steady_clock::now();
    auto activated_net_group = VdmaConfigActivatedNetworkGroup::create(
        m_active_net_group_holder, name(), m_resources_managers, network_group_params, dynamic_batch_size,
        m_input_streams, m_output_streams, m_network_group_activated_event, m_deactivation_time_accumulator);
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

hailo_status VdmaConfigNetworkGroup::create_vdevice_streams_from_config_params(std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle)
{
    // TODO - HRT-6931 - raise error on this case 
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_resources_managers.size())) {
        LOGGER__WARNING("Latency measurement is not supported on more than 1 physical device.");
    }

    m_multiplexer = multiplexer;

    for (const auto &stream_parameters_pair : m_config_params.stream_params_by_name) {
        switch (stream_parameters_pair.second.direction) {
            case HAILO_H2D_STREAM:
                {
                    auto status = create_input_vdevice_stream_from_config_params(stream_parameters_pair.second,
                        stream_parameters_pair.first, multiplexer, scheduler_handle);
                    CHECK_SUCCESS(status);
                }
                break;
            case HAILO_D2H_STREAM:
                {
                    auto status = create_output_vdevice_stream_from_config_params(stream_parameters_pair.second,
                        stream_parameters_pair.first, multiplexer, scheduler_handle);
                    CHECK_SUCCESS(status);
                }
                break;
            default:
                LOGGER__ERROR("stream name {} direction is invalid.", stream_parameters_pair.first);
                return HAILO_INVALID_ARGUMENT;
        }
    }

    auto status = m_multiplexer->add_network_group_instance(m_multiplexer_handle, *this);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::create_input_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    CHECK(HailoRTCommon::is_vdma_stream_interface(stream_params.stream_interface), HAILO_INVALID_OPERATION,
        "Stream {} not supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);
    auto input_stream = VDeviceInputStream::create(m_resources_managers, edge_layer.value(),
        stream_name, scheduler_handle, m_network_group_activated_event,
        m_network_group_scheduler);
    CHECK_EXPECTED_AS_STATUS(input_stream);
    auto input_stream_wrapper = VDeviceInputStreamWrapper::create(input_stream.release(), edge_layer->network_name, multiplexer, scheduler_handle);
    CHECK_EXPECTED_AS_STATUS(input_stream_wrapper);
    m_input_streams.insert(make_pair(stream_name, input_stream_wrapper.release()));

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::create_output_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    CHECK(HailoRTCommon::is_vdma_stream_interface(stream_params.stream_interface), HAILO_INVALID_OPERATION,
        "Stream {} not supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);
    auto output_stream = VDeviceOutputStream::create(m_resources_managers, edge_layer.value(),
        stream_name, scheduler_handle, m_network_group_activated_event,
        m_network_group_scheduler);
    CHECK_EXPECTED_AS_STATUS(output_stream);
    auto output_stream_wrapper = VDeviceOutputStreamWrapper::create(output_stream.release(), edge_layer->network_name, multiplexer, scheduler_handle);
    CHECK_EXPECTED_AS_STATUS(output_stream_wrapper);
    m_output_streams.insert(make_pair(stream_name, output_stream_wrapper.release()));

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::create_vdevice_streams_from_duplicate(std::shared_ptr<VdmaConfigNetworkGroup> other)
{
    // TODO - HRT-6931 - raise error on this case 
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_resources_managers.size())) {
        LOGGER__WARNING("Latency measurement is not supported on more than 1 physical device.");
    }

    m_multiplexer = other->m_multiplexer;
    m_multiplexer_handle = other->multiplexer_duplicates_count() + 1;

    for (auto &name_stream_pair : other->m_input_streams) {
        auto input_stream = static_cast<VDeviceInputStreamWrapper*>(name_stream_pair.second.get());
        auto copy = input_stream->clone(m_multiplexer_handle);
        CHECK_EXPECTED_AS_STATUS(copy);

        m_input_streams.insert(make_pair(name_stream_pair.first, copy.release()));
    }

    for (auto &name_stream_pair : other->m_output_streams) {
        auto output_stream = static_cast<VDeviceOutputStreamWrapper*>(name_stream_pair.second.get());
        auto copy = output_stream->clone(m_multiplexer_handle);
        CHECK_EXPECTED_AS_STATUS(copy);

        m_output_streams.insert(make_pair(name_stream_pair.first, copy.release()));
    }

    auto status = other->m_multiplexer->add_network_group_instance(m_multiplexer_handle, *this);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void VdmaConfigNetworkGroup::set_network_group_handle(scheduler_ng_handle_t handle)
{
    m_scheduler_handle = handle;
}

scheduler_ng_handle_t VdmaConfigNetworkGroup::network_group_handle() const
{
    return m_scheduler_handle;
}

hailo_status VdmaConfigNetworkGroup::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    CHECK(network_group_scheduler, HAILO_INVALID_OPERATION,
        "Cannot set scheduler timeout for network group {}, as it is configured on a vdevice which does not have scheduling enabled", name());
    if (network_name != HailoRTDefaults::get_network_name(name())) {
        CHECK(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting scheduler timeout for a specific network is currently not supported");
    }
    auto status = network_group_scheduler->set_timeout(m_scheduler_handle, timeout, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status VdmaConfigNetworkGroup::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    CHECK(network_group_scheduler, HAILO_INVALID_OPERATION,
        "Cannot set scheduler threshold for network group {}, as it is configured on a vdevice which does not have scheduling enabled", name());
    if (network_name != HailoRTDefaults::get_network_name(name())) {
        CHECK(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting scheduler threshold for a specific network is currently not supported");
    }
    auto status = network_group_scheduler->set_threshold(m_scheduler_handle, threshold, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<LatencyMetersMap>> VdmaConfigNetworkGroup::get_latency_meters()
{
    auto latency_meters = m_resources_managers[0]->get_latency_meters();
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

Expected<std::vector<OutputVStream>> VdmaConfigNetworkGroup::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    auto expected = ConfiguredNetworkGroupBase::create_output_vstreams(outputs_params);
    CHECK_EXPECTED(expected);

    if (nullptr == m_multiplexer) {
        return expected.release();
    }

    m_multiplexer->set_output_vstreams_names(m_multiplexer_handle, expected.value());

    for (auto &vstream : expected.value()) {
        static_cast<OutputVStreamImpl&>(*vstream.m_vstream).set_on_vstream_cant_read_callback([this, name = vstream.name()] () {
            m_multiplexer->set_can_output_vstream_read(m_multiplexer_handle, name, false);
        });
        static_cast<OutputVStreamImpl&>(*vstream.m_vstream).set_on_vstream_can_read_callback([this, name = vstream.name()] () {
            m_multiplexer->set_can_output_vstream_read(m_multiplexer_handle, name, true);
        });
    }

    return expected.release();
}

} /* namespace hailort */
