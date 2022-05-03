#include "hailo/transform.hpp"
#include "network_group_internal.hpp"
#include "network_group_wrapper.hpp"
#include "hef_internal.hpp"
#include "common/utils.hpp"
#include "hailort_defaults.hpp"
#include "eth_stream.hpp"
#include "pcie_stream.hpp"
#include "core_stream.hpp"
#include "mipi_stream.hpp"
#include "control.hpp"
#include "common/runtime_statistics_internal.hpp"

namespace hailort
{

ActivatedNetworkGroupBase::ActivatedNetworkGroupBase(const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::unique_ptr<InputStream>> &input_streams, std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,         
        EventPtr &&network_group_activated_event, hailo_status &status) :
    m_network_group_params(network_group_params),
    m_input_streams(input_streams),
    m_output_streams(output_streams),
    m_network_group_activated_event(std::move(network_group_activated_event))
{
    status = validate_network_group_params(network_group_params);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to validate network_group params");
        return;
    }

    status = activate_low_level_streams();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to activate low level streams");
        return;
    }

    status = m_network_group_activated_event->signal();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to signal network activation event");
        return;
    }
}

Expected<LatencyMeterPtr> ConfiguredNetworkGroupBase::create_hw_latency_meter(Device &device,
    const std::vector<LayerInfo> &layers)
{
    std::set<uint32_t> d2h_channel_indexes;

    // TODO: dont support hw latency meter with MIPI input

    if (Device::Type::PCIE != device.get_type()) {
        LOGGER__WARNING("HW Latency measurement is supported only on PCIe devices");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    size_t h2d_streams_count = 0;
    for (const auto &layer : layers) {
        if (layer.direction == HAILO_D2H_STREAM) {
            if (HAILO_FORMAT_ORDER_HAILO_NMS == layer.format.order) {
                LOGGER__WARNING("HW Latency measurement is not supported on NMS networks");
                return make_unexpected(HAILO_INVALID_OPERATION);
            }

            d2h_channel_indexes.insert(layer.index);
        }
        else {
            h2d_streams_count++;
        }
    }

    if (h2d_streams_count > 1) {
        LOGGER__WARNING("HW Latency measurement is supported on networks with a single input");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    return make_shared_nothrow<LatencyMeter>(d2h_channel_indexes, MAX_IRQ_TIMESTAMPS_SIZE);
}

hailo_status ActivatedNetworkGroupBase::activate_low_level_streams()
{
    for (auto &name_pair : m_input_streams) {
        auto status = name_pair.second->activate_stream();
        CHECK_SUCCESS(status);
    }
    for (auto &name_pair : m_output_streams) {
        auto status = name_pair.second->activate_stream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

uint32_t ActivatedNetworkGroupBase::get_invalid_frames_count()
{
    uint32_t total_invalid_frames_count = 0;
    for (auto& name_stream_pair : m_output_streams) {
        total_invalid_frames_count += name_stream_pair.second->get_invalid_frames_count();
    }
    return total_invalid_frames_count;
}

void ActivatedNetworkGroupBase::deactivate_resources()
{
    if (nullptr != m_network_group_activated_event) {
        for (auto &name_pair : m_input_streams) {
            auto status = name_pair.second->deactivate_stream();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to deactivate input stream name {}", name_pair.first);
            }
        }
    
        for (auto &name_pair : m_output_streams) {
            auto status = name_pair.second->deactivate_stream();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to deactivate output stream name {}", name_pair.first);
            }
        }
        m_network_group_activated_event->reset();
    }

}

// TODO: Implement function (HRT-3174)
hailo_status ActivatedNetworkGroupBase::validate_network_group_params(
    const hailo_activate_network_group_params_t &/*network_group_params*/)
{
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroup::activate()
{
    const auto network_group_params = HailoRTDefaults::get_network_group_params();
    return activate(network_group_params);
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupBase::activate(
    const hailo_activate_network_group_params_t &network_group_params)
{
    CHECK_AS_EXPECTED(!m_is_scheduling, HAILO_INVALID_OPERATION,
        "Manually activating a network group is not allowed when the network group scheduler is active!");
    return activate_internal(network_group_params, CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE);
}

Expected<std::chrono::nanoseconds> get_latency(LatencyMeterPtr &latency_meter, bool clear)
{
    auto hw_latency = latency_meter->get_latency(clear);
    if (HAILO_NOT_AVAILABLE == hw_latency.status()) {
        return make_unexpected(HAILO_NOT_AVAILABLE);
    }
    CHECK_EXPECTED(hw_latency, "Failed getting latency");
    return hw_latency.release();
}

/* Network group base functions */
Expected<LatencyMeasurementResult> ConfiguredNetworkGroupBase::get_latency_measurement(const std::string &network_name)
{
    bool clear = ((m_config_params.latency & HAILO_LATENCY_CLEAR_AFTER_GET) == HAILO_LATENCY_CLEAR_AFTER_GET);
    LatencyMeasurementResult result = {};

    if (network_name.empty()) {
        std::chrono::nanoseconds latency_sum(0);
        uint32_t measurements_count = 0;
        for (auto &latency_meter_pair : m_latency_meter) {
            auto hw_latency = get_latency(latency_meter_pair.second, clear);
            if (HAILO_NOT_AVAILABLE == hw_latency.status()) {
                continue;
            }
            CHECK_EXPECTED(hw_latency);
            latency_sum += hw_latency.value();
            measurements_count++;
        }
        if (0 == measurements_count) {
            LOGGER__DEBUG("No latency measurements was found");
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        result.avg_hw_latency = latency_sum / measurements_count;
    } else {
        if(!contains(m_latency_meter, network_name)) {
            LOGGER__DEBUG("No latency measurements was found for network {}", network_name);
            return make_unexpected(HAILO_NOT_FOUND);
        }
        auto hw_latency = get_latency(m_latency_meter.at(network_name), clear);
        if (HAILO_NOT_AVAILABLE == hw_latency.status()) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        CHECK_EXPECTED(hw_latency);
        result.avg_hw_latency = hw_latency.value();
    }
    return result;
}

Expected<OutputStreamWithParamsVector> ConfiguredNetworkGroupBase::get_output_streams_from_vstream_names(
    const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    OutputStreamWithParamsVector results;
    std::unordered_map<std::string, hailo_vstream_params_t> outputs_edges_params;
    for (auto &name_params_pair : outputs_params) {
        auto stream_names = m_network_group_metadata.get_stream_names_from_vstream_name(name_params_pair.first);
        CHECK_EXPECTED(stream_names);

        for (auto &stream_name : stream_names.value()) {
            auto output_stream = get_output_stream_by_name(stream_name);
            CHECK_EXPECTED(output_stream);

            if (output_stream->get().get_info().is_mux) {
                outputs_edges_params.emplace(name_params_pair);
            }
            else {
                NameToVStreamParamsMap name_to_params = {name_params_pair};
                results.emplace_back(output_stream.release(), name_to_params);
            }
        }
    }
    // Add non mux streams to result
    hailo_status status = add_mux_streams_by_edges_names(results, outputs_edges_params); 
    CHECK_SUCCESS_AS_EXPECTED(status);

    return results;
}

// This function adds to results the OutputStreams that correspond to the edges in outputs_edges_params.
// If an edge name appears in outputs_edges_params then all of its predecessors must appear in outputs_edges_params as well, Otherwise, an error is returned.
// We use the set seen_edges in order to mark the edges already evaluated by one of its' predecessor.
hailo_status ConfiguredNetworkGroupBase::add_mux_streams_by_edges_names(OutputStreamWithParamsVector &results,
    const std::unordered_map<std::string, hailo_vstream_params_t> &outputs_edges_params)
{
    std::unordered_set<std::string> seen_edges;
    for (auto &name_params_pair : outputs_edges_params) {
        if (seen_edges.end() != seen_edges.find(name_params_pair.first)) {
            // Edge has already been seen by one of its predecessors
            continue;
        }
        auto output_streams = get_output_streams_by_vstream_name(name_params_pair.first);
        CHECK_EXPECTED_AS_STATUS(output_streams);
        CHECK(output_streams->size() == 1, HAILO_INVALID_ARGUMENT,
            "mux streams cannot be separated into multiple streams");
        auto output_stream = output_streams.release()[0];

        // TODO: Find a better way to get the mux edges without creating OutputDemuxer
        auto expected_demuxer = OutputDemuxer::create(output_stream.get());
        CHECK_EXPECTED_AS_STATUS(expected_demuxer);

        NameToVStreamParamsMap name_to_params;
        for (auto &edge : expected_demuxer.value()->get_edges_stream_info()) {
            auto edge_name_params_pair = outputs_edges_params.find(edge.name);
            CHECK(edge_name_params_pair != outputs_edges_params.end(), HAILO_INVALID_ARGUMENT,
                "All edges of stream {} must be in output vstream params. edge {} is missing.",
                name_params_pair.first, edge.name);
            seen_edges.insert(edge.name);
            name_to_params.insert(*edge_name_params_pair);
        }
        results.emplace_back(output_stream, name_to_params);
    }
    return HAILO_SUCCESS;
}

Expected<OutputStreamRefVector> ConfiguredNetworkGroupBase::get_output_streams_by_vstream_name(const std::string &name)
{
    auto stream_names = m_network_group_metadata.get_stream_names_from_vstream_name(name);
    CHECK_EXPECTED(stream_names);

    OutputStreamRefVector output_streams;
    output_streams.reserve(stream_names->size());
    for (const auto &stream_name : stream_names.value()) {
        auto output_stream = get_output_stream_by_name(stream_name);
        CHECK_EXPECTED(output_stream);

        output_streams.emplace_back(output_stream.release());
    }

    return output_streams;
}

Expected<LayerInfo> ConfiguredNetworkGroupBase::get_layer_info(const std::string &stream_name)
{
    auto layer_infos = m_network_group_metadata.get_all_layer_infos();
    CHECK_EXPECTED(layer_infos);
    for (auto layer_info : layer_infos.release()) {
        if (layer_info.name == stream_name) {
            return layer_info;
        }
    }
    LOGGER__ERROR("Failed to find layer with name {}", stream_name);
    return make_unexpected(HAILO_NOT_FOUND);
}

ConfiguredNetworkGroupBase::ConfiguredNetworkGroupBase(
    const ConfigureNetworkParams &config_params, const uint8_t net_group_index, 
    const NetworkGroupMetadata &network_group_metadata, hailo_status &status) :
        ConfiguredNetworkGroupBase(config_params, net_group_index, network_group_metadata, false, status)
{}

ConfiguredNetworkGroupBase::ConfiguredNetworkGroupBase(
    const ConfigureNetworkParams &config_params, const uint8_t net_group_index, 
    const NetworkGroupMetadata &network_group_metadata, bool is_scheduling, hailo_status &status) :
        m_config_params(config_params),
        m_net_group_index(net_group_index),
        m_latency_meter(),
        m_network_group_metadata(network_group_metadata),
        m_activation_time_accumulator(),
        m_deactivation_time_accumulator(),
        m_is_scheduling(is_scheduling)
{
    // TODO: Add validation for config_params is running with schedular and hence dynamic_batch_size (HRT-6538)
    auto event = Event::create_shared(Event::State::not_signalled);
    if (nullptr == event) {
        LOGGER__ERROR("Failed to create activation event");
        status = HAILO_INTERNAL_FAILURE;
        return;
    }
    m_network_group_activated_event = std::move(std::move(event));

    m_activation_time_accumulator = make_shared_nothrow<FullAccumulator<double>>("activation_time");
    if (nullptr == m_activation_time_accumulator) {
        LOGGER__ERROR("Failed to create activation time accumulator");
        status = HAILO_OUT_OF_HOST_MEMORY;
        return;
    };

    m_deactivation_time_accumulator = make_shared_nothrow<FullAccumulator<double>>("deactivation_time");
    if (nullptr == m_deactivation_time_accumulator) {
        LOGGER__ERROR("Failed to create deactivation time accumulator");
        status = HAILO_OUT_OF_HOST_MEMORY;
        return;
    };

    status = HAILO_SUCCESS;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupBase::activate_internal(
    const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size)
{
    CHECK_AS_EXPECTED(dynamic_batch_size <= m_config_params.batch_size, HAILO_INVALID_ARGUMENT,
        "Dynamic batch size ({}) must be less than/equal to the configured batch size ({})",
        dynamic_batch_size, m_config_params.batch_size);
    return activate_impl(network_group_params, dynamic_batch_size);
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupBase::force_activate(uint16_t dynamic_batch_size)
{
    return activate_internal(HailoRTDefaults::get_network_group_params(), dynamic_batch_size);
}

const std::string &ConfiguredNetworkGroupBase::get_network_group_name() const
{
    return m_network_group_metadata.network_group_name();
}

Expected<uint16_t> ConfiguredNetworkGroupBase::get_stream_batch_size(const std::string &stream_name)
{
    auto layer_infos = m_network_group_metadata.get_all_layer_infos();
    CHECK_EXPECTED(layer_infos);
    for (const auto &layer_info : layer_infos.release()) {
        if (layer_info.name == stream_name) {
            for (auto const &network_params_pair : m_config_params.network_params_by_name) {
                if (network_params_pair.first == layer_info.network_name) {
                    auto batch_size = network_params_pair.second.batch_size;
                    return batch_size;
                }
            }
        }
    }
    LOGGER__ERROR("Failed to find network name output stream {}", stream_name);
    return make_unexpected(HAILO_NOT_FOUND);
}

const ConfigureNetworkParams ConfiguredNetworkGroupBase::get_config_params() const
{
    return m_config_params;
}

hailo_status ConfiguredNetworkGroupBase::create_input_stream_from_config_params(Device &device,
    const hailo_stream_parameters_t &stream_params, const std::string &stream_name)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    auto latency_meter = (contains(m_latency_meter, edge_layer->network_name)) ? m_latency_meter.at(edge_layer->network_name) : nullptr;

    CHECK(device.is_stream_interface_supported(stream_params.stream_interface), HAILO_INVALID_OPERATION,
        "Device does not supports the given stream interface streams. Please update input_stream_params for stream {}.",
        stream_name);

    switch (stream_params.stream_interface) {
        case HAILO_STREAM_INTERFACE_PCIE:
            {
                auto batch_size_exp = get_stream_batch_size(stream_name);
                CHECK_EXPECTED_AS_STATUS(batch_size_exp);
                const auto stream_index = edge_layer->index;
                const auto channel_index = get_boundary_channel_index(stream_index, HAILO_H2D_STREAM, stream_name);
                CHECK_EXPECTED_AS_STATUS(channel_index, "Failed to get channel index for input stream {}", stream_index);

                auto input_stream = PcieInputStream::create(device, channel_index.value(),
                    edge_layer.value(), batch_size_exp.value(), m_network_group_activated_event, latency_meter);
                CHECK_EXPECTED_AS_STATUS(input_stream);
                m_input_streams.insert(make_pair(stream_name, input_stream.release()));
            }
            break;
        case HAILO_STREAM_INTERFACE_CORE:
            {
                auto batch_size_exp = get_stream_batch_size(stream_name);
                CHECK_EXPECTED_AS_STATUS(batch_size_exp);
                const auto stream_index = edge_layer->index;
                const auto channel_index = get_boundary_channel_index(stream_index, HAILO_H2D_STREAM, stream_name);
                CHECK_EXPECTED_AS_STATUS(channel_index, "Failed to get channel index for input stream {}", stream_index);

                auto input_stream = CoreInputStream::create(device, channel_index.value(),
                    edge_layer.value(), batch_size_exp.value(), m_network_group_activated_event, latency_meter);
                CHECK_EXPECTED_AS_STATUS(input_stream);
                m_input_streams.insert(make_pair(stream_name, input_stream.release()));
            }
            break;
        case HAILO_STREAM_INTERFACE_ETH:
            {
                auto input_stream = EthernetInputStream::create(device,
                    edge_layer.value(), stream_params.eth_input_params, m_network_group_activated_event);
                CHECK_EXPECTED_AS_STATUS(input_stream);
                m_input_streams.insert(make_pair(stream_name, input_stream.release()));
            }
            break;
        case HAILO_STREAM_INTERFACE_MIPI:
            {
                auto input_stream = MipiInputStream::create(device,
                    edge_layer.value(), stream_params.mipi_input_params, m_network_group_activated_event);
                CHECK_EXPECTED_AS_STATUS(input_stream);
                m_input_streams.insert(make_pair(stream_name, input_stream.release()));
            }
            break;
        default:
            {
                LOGGER__ERROR("{} interface is not supported.", stream_params.stream_interface);
                return HAILO_NOT_IMPLEMENTED;
            }
    }

    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupBase::create_output_stream_from_config_params(Device &device,
    const hailo_stream_parameters_t &stream_params, const std::string &stream_name)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    auto latency_meter = (contains(m_latency_meter, edge_layer->network_name)) ? m_latency_meter.at(edge_layer->network_name) : nullptr;

    CHECK(device.is_stream_interface_supported(stream_params.stream_interface), HAILO_INVALID_OPERATION,
        "Device does not supports the given stream interface streams. Please update input_stream_params for stream {}.",
        stream_name);

    switch (stream_params.stream_interface) {
        case HAILO_STREAM_INTERFACE_PCIE:
            {
                auto batch_size_exp = get_stream_batch_size(stream_name);
                CHECK_EXPECTED_AS_STATUS(batch_size_exp);
                const auto stream_index = edge_layer->index;
                const auto channel_index = get_boundary_channel_index(stream_index, HAILO_D2H_STREAM, stream_name);
                CHECK_EXPECTED_AS_STATUS(channel_index, "Failed to get channel index for output stream {}", stream_index);

                auto output_stream = PcieOutputStream::create(device, channel_index.value(),
                    edge_layer.value(), batch_size_exp.value(), m_network_group_activated_event, latency_meter);
                CHECK_EXPECTED_AS_STATUS(output_stream);
                m_output_streams.insert(make_pair(stream_name, output_stream.release()));
            }
            break;
        case HAILO_STREAM_INTERFACE_CORE:
            {
                auto batch_size_exp = get_stream_batch_size(stream_name);
                CHECK_EXPECTED_AS_STATUS(batch_size_exp);
                const auto stream_index = edge_layer->index;
                const auto channel_index = get_boundary_channel_index(stream_index, HAILO_D2H_STREAM, stream_name);
                CHECK_EXPECTED_AS_STATUS(channel_index, "Failed to get channel index for output stream {}", stream_index);

                auto output_stream = CoreOutputStream::create(device, channel_index.value(),
                    edge_layer.value(), batch_size_exp.value(), m_network_group_activated_event,
                    latency_meter);
                CHECK_EXPECTED_AS_STATUS(output_stream);
                m_output_streams.insert(make_pair(stream_name, output_stream.release()));
            }
            break;
        case HAILO_STREAM_INTERFACE_ETH:
            {
                auto output_stream =  EthernetOutputStream::create(device,
                    edge_layer.value(), stream_params.eth_output_params, 
                    m_network_group_activated_event);
                CHECK_EXPECTED_AS_STATUS(output_stream);
                m_output_streams.insert(make_pair(stream_name, output_stream.release()));
            }
            break;
        default:
            {
                LOGGER__ERROR("{} interface is not supported.", stream_params.stream_interface);
                return HAILO_NOT_IMPLEMENTED;
            }
    }

    return HAILO_SUCCESS;
}

hailo_status ConfiguredNetworkGroupBase::create_streams_from_config_params(Device &device)
{
    if ((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) {
        // Best affort for starting latency meter.
        auto networks_names = m_network_group_metadata.get_network_names();
        for (auto &network_name : networks_names) {
            auto layer_infos = m_network_group_metadata.get_all_layer_infos(network_name);
            CHECK_EXPECTED_AS_STATUS(layer_infos);
            auto latency_meter = ConfiguredNetworkGroupBase::create_hw_latency_meter(device, layer_infos.value());
            if (latency_meter) {
                m_latency_meter.emplace(network_name, latency_meter.release());
                LOGGER__DEBUG("Starting hw latency measurement for network {}", network_name);
            }
        }
    }

    for (const auto &stream_parameters_pair : m_config_params.stream_params_by_name) {
        switch (stream_parameters_pair.second.direction) {
            case HAILO_H2D_STREAM:
                {
                    auto status = create_input_stream_from_config_params(device,
                        stream_parameters_pair.second,
                        stream_parameters_pair.first);
                    CHECK_SUCCESS(status);
                }
                break;
            case HAILO_D2H_STREAM:
                {
                    auto status = create_output_stream_from_config_params(device,
                    stream_parameters_pair.second,
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

Expected<InputStreamRefVector> ConfiguredNetworkGroupBase::get_input_streams_by_network(const std::string &network_name)
{
    auto input_stream_infos = m_network_group_metadata.get_input_stream_infos(network_name);
    CHECK_EXPECTED(input_stream_infos);

    InputStreamRefVector result;
    for (auto &stream_info : input_stream_infos.value()) {
        auto stream_ref = get_input_stream_by_name(stream_info.name);
        CHECK_EXPECTED(stream_ref);
        result.push_back(stream_ref.release());
    }
    return result;
}

Expected<OutputStreamRefVector> ConfiguredNetworkGroupBase::get_output_streams_by_network(const std::string &network_name)
{
    auto output_stream_infos = m_network_group_metadata.get_output_stream_infos(network_name);
    CHECK_EXPECTED(output_stream_infos);

    OutputStreamRefVector result;
    for (auto &stream_info : output_stream_infos.value()) {
        auto stream_ref = get_output_stream_by_name(stream_info.name);
        CHECK_EXPECTED(stream_ref);
        result.push_back(stream_ref.release());
    }
    return result;
}

InputStreamRefVector ConfiguredNetworkGroupBase::get_input_streams()
{
    InputStreamRefVector result;
    for (auto& name_stream_pair : m_input_streams) {
        result.emplace_back(std::ref(*name_stream_pair.second));
    }
    return result;
}

OutputStreamRefVector ConfiguredNetworkGroupBase::get_output_streams()
{
    OutputStreamRefVector result;
    for (auto& name_stream_pair : m_output_streams) {
        result.emplace_back(std::ref(*name_stream_pair.second));
    }
    return result;
}

ExpectedRef<InputStream> ConfiguredNetworkGroupBase::get_input_stream_by_name(const std::string& name)
{
    auto iterator = m_input_streams.find(name);
    if (m_input_streams.end() == iterator) {
        LOGGER__ERROR("Input stream name {} not found", name);
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref<InputStream>(*iterator->second);
}

ExpectedRef<OutputStream> ConfiguredNetworkGroupBase::get_output_stream_by_name(const std::string& name)
{
    auto iterator = m_output_streams.find(name);
    if (m_output_streams.end() == iterator) {
        LOGGER__ERROR("Output stream name {} not found", name);
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return std::ref<OutputStream>(*iterator->second);
}

std::vector<std::reference_wrapper<InputStream>> ConfiguredNetworkGroupBase::get_input_streams_by_interface(
    hailo_stream_interface_t stream_interface)
{
    std::vector<std::reference_wrapper<InputStream>> results;
    for (auto &name_pair : m_input_streams) {
        if (stream_interface == name_pair.second->get_interface()) {
            results.push_back(std::ref(*name_pair.second));
        }
    }
    return results;
}

std::vector<std::reference_wrapper<OutputStream>> ConfiguredNetworkGroupBase::get_output_streams_by_interface(
    hailo_stream_interface_t stream_interface)
{
    std::vector<std::reference_wrapper<OutputStream>> results;
    for (auto &name_pair : m_output_streams) {
        if (stream_interface == name_pair.second->get_interface()) {
            results.push_back(std::ref(*name_pair.second));
        }
    }
    return results;
}

hailo_status ConfiguredNetworkGroupBase::wait_for_activation(const std::chrono::milliseconds &timeout)
{
    return m_network_group_activated_event->wait(timeout);
}

Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroupBase::get_output_vstream_groups()
{
    std::vector<std::vector<std::string>> results;

    for (auto output_stream : get_output_streams()) {
        auto vstreams_group = get_vstream_names_from_stream_name(output_stream.get().name());
        CHECK_EXPECTED(vstreams_group);
        results.push_back(vstreams_group.release());
    }

    return results;
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroupBase::make_output_vstream_params_groups(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    auto params = make_output_vstream_params(quantized, format_type, timeout_ms, queue_size);
    CHECK_EXPECTED(params);

    auto groups = get_output_vstream_groups();
    CHECK_EXPECTED(groups);

    std::vector<std::map<std::string, hailo_vstream_params_t>> results(groups->size(), std::map<std::string, hailo_vstream_params_t>());

    size_t pipeline_group_index = 0;
    for (const auto &group : groups.release()) {
        for (const auto &name_pair : params.value()) {
            if (contains(group, name_pair.first)) {
                results[pipeline_group_index].insert(name_pair);
            }
        }
        pipeline_group_index++;
    }

    return results;
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupBase::make_input_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    auto input_vstream_infos = m_network_group_metadata.get_input_vstream_infos(network_name);
    CHECK_EXPECTED(input_vstream_infos);

    std::map<std::string, hailo_vstream_params_t> res;
    auto status = Hef::Impl::fill_missing_vstream_params_with_default(res, input_vstream_infos.value(), quantized, 
        format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupBase::make_output_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    auto output_vstream_infos = m_network_group_metadata.get_output_vstream_infos(network_name);
    CHECK_EXPECTED(output_vstream_infos);

    std::map<std::string, hailo_vstream_params_t> res;
    auto status = Hef::Impl::fill_missing_vstream_params_with_default(res, output_vstream_infos.value(), quantized, 
        format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroupBase::get_network_infos() const
{
    return m_network_group_metadata.get_network_infos();
}

Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroupBase::get_all_stream_infos(
    const std::string &network_name) const
{
    return m_network_group_metadata.get_all_stream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_input_vstream_infos(
    const std::string &network_name) const
{
    return m_network_group_metadata.get_input_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_output_vstream_infos(
    const std::string &network_name) const
{
    return m_network_group_metadata.get_output_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupBase::get_all_vstream_infos(
    const std::string &network_name) const
{
    return m_network_group_metadata.get_all_vstream_infos(network_name);
}

AccumulatorPtr ConfiguredNetworkGroupBase::get_activation_time_accumulator() const
{
    return m_activation_time_accumulator;
}

AccumulatorPtr ConfiguredNetworkGroupBase::get_deactivation_time_accumulator() const
{
    return m_deactivation_time_accumulator;
}

const std::string &ConfiguredNetworkGroupWrapper::get_network_group_name() const
{
    return m_configured_network_group->get_network_group_name();
}

Expected<hailo_stream_interface_t> ConfiguredNetworkGroupWrapper::get_default_streams_interface()
{
    return m_configured_network_group->get_default_streams_interface();
}

std::vector<std::reference_wrapper<InputStream>> ConfiguredNetworkGroupWrapper::get_input_streams_by_interface(hailo_stream_interface_t stream_interface)
{
    return m_configured_network_group->get_input_streams_by_interface(stream_interface);
}

std::vector<std::reference_wrapper<OutputStream>> ConfiguredNetworkGroupWrapper::get_output_streams_by_interface(hailo_stream_interface_t stream_interface)
{
    return m_configured_network_group->get_output_streams_by_interface(stream_interface);
}

ExpectedRef<InputStream> ConfiguredNetworkGroupWrapper::get_input_stream_by_name(const std::string& name)
{
    return m_configured_network_group->get_input_stream_by_name(name);
}
ExpectedRef<OutputStream> ConfiguredNetworkGroupWrapper::get_output_stream_by_name(const std::string& name)
{
    return m_configured_network_group->get_output_stream_by_name(name);
}

Expected<InputStreamRefVector> ConfiguredNetworkGroupWrapper::get_input_streams_by_network(const std::string &network_name)
{
    return m_configured_network_group->get_input_streams_by_network(network_name);
}

Expected<OutputStreamRefVector> ConfiguredNetworkGroupWrapper::get_output_streams_by_network(const std::string &network_name)
{
    return m_configured_network_group->get_output_streams_by_network(network_name);
}

InputStreamRefVector ConfiguredNetworkGroupWrapper::get_input_streams()
{
    return m_configured_network_group->get_input_streams();
}

OutputStreamRefVector ConfiguredNetworkGroupWrapper::get_output_streams()
{
    return m_configured_network_group->get_output_streams();
}

Expected<LatencyMeasurementResult> ConfiguredNetworkGroupWrapper::get_latency_measurement(const std::string &network_name)
{
    return m_configured_network_group->get_latency_measurement(network_name);
}

Expected<OutputStreamWithParamsVector> ConfiguredNetworkGroupWrapper::get_output_streams_from_vstream_names(
    const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    return m_configured_network_group->get_output_streams_from_vstream_names(outputs_params);
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupWrapper::activate_internal(const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size)
{
    return m_configured_network_group->activate_internal(network_group_params, dynamic_batch_size);
}

hailo_status ConfiguredNetworkGroupWrapper::wait_for_activation(const std::chrono::milliseconds &timeout)
{
    return m_configured_network_group->wait_for_activation(timeout);
}

Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupWrapper::make_input_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_configured_network_group->make_input_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
}
Expected<std::map<std::string, hailo_vstream_params_t>> ConfiguredNetworkGroupWrapper::make_output_vstream_params(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
    const std::string &network_name)
{
    return m_configured_network_group->make_output_vstream_params(quantized, format_type, timeout_ms, queue_size, network_name);
}

Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> ConfiguredNetworkGroupWrapper::make_output_vstream_params_groups(
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    return m_configured_network_group->make_output_vstream_params_groups(quantized, format_type, timeout_ms, queue_size);
}

Expected<std::vector<std::vector<std::string>>> ConfiguredNetworkGroupWrapper::get_output_vstream_groups()
{
    return m_configured_network_group->get_output_vstream_groups();
}

Expected<std::vector<hailo_network_info_t>> ConfiguredNetworkGroupWrapper::get_network_infos() const
{
    return m_configured_network_group->get_network_infos();
}

Expected<std::vector<hailo_stream_info_t>> ConfiguredNetworkGroupWrapper::get_all_stream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_all_stream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupWrapper::get_input_vstream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_input_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupWrapper::get_output_vstream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_output_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> ConfiguredNetworkGroupWrapper::get_all_vstream_infos(const std::string &network_name) const
{
    return m_configured_network_group->get_all_vstream_infos(network_name);
}

AccumulatorPtr ConfiguredNetworkGroupWrapper::get_activation_time_accumulator() const
{
    return m_configured_network_group->get_activation_time_accumulator();
}

AccumulatorPtr ConfiguredNetworkGroupWrapper::get_deactivation_time_accumulator() const
{
    return m_configured_network_group->get_deactivation_time_accumulator();
}

std::shared_ptr<ConfiguredNetworkGroupBase> ConfiguredNetworkGroupWrapper::get_configured_network() const
{
    return m_configured_network_group;
}

ConfiguredNetworkGroupWrapper::ConfiguredNetworkGroupWrapper(std::shared_ptr<ConfiguredNetworkGroupBase> configured_network_group, hailo_status &status) :
    m_configured_network_group(configured_network_group)
{
    // Can't fail for now
    status = HAILO_SUCCESS;
}

Expected<ConfiguredNetworkGroupWrapper> ConfiguredNetworkGroupWrapper::create(std::shared_ptr<ConfiguredNetworkGroupBase> configured_network_group)
{
    hailo_status status = HAILO_UNINITIALIZED;

    auto wrapper = ConfiguredNetworkGroupWrapper(configured_network_group, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return wrapper;
}

Expected<ConfiguredNetworkGroupWrapper> ConfiguredNetworkGroupWrapper::clone()
{
    auto wrapper = create(m_configured_network_group);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> ConfiguredNetworkGroupWrapper::activate(const hailo_activate_network_group_params_t &network_group_params)
{
    return m_configured_network_group->activate(network_group_params);
}

} /* namespace hailort */
