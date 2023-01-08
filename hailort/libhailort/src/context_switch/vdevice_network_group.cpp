/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_network_group.cpp
 * @brief: Network Group Wrapper
 **/

#include "vdevice_network_group.hpp"
#include "vdevice_stream.hpp"
#include "vdevice_stream_multiplexer_wrapper.hpp"
#include "vstream_internal.hpp"
#include "tracer_macros.hpp"

namespace hailort
{

Expected<std::unique_ptr<ActivatedNetworkGroup>> VDeviceActivatedNetworkGroup::create(
    std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> configured_network_groups,
    std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
    std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
    const hailo_activate_network_group_params_t &network_group_params,
    EventPtr network_group_activated_event, uint16_t dynamic_batch_size,
    AccumulatorPtr deactivation_time_accumulator)
{
    auto status = HAILO_UNINITIALIZED;
    std::vector<std::unique_ptr<ActivatedNetworkGroup>> activated_network_groups;
    activated_network_groups.reserve(configured_network_groups.size());
    for (auto cng : configured_network_groups) {
        auto ang = cng->create_activated_network_group(network_group_params, dynamic_batch_size);
        CHECK_EXPECTED(ang);
        activated_network_groups.emplace_back(ang.release());
    }
    auto ang = VDeviceActivatedNetworkGroup(std::move(activated_network_groups), input_streams, output_streams,
        network_group_params, network_group_activated_event, deactivation_time_accumulator, status);

    CHECK_SUCCESS_AS_EXPECTED(status);
    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr =
        make_unique_nothrow<VDeviceActivatedNetworkGroup>(std::move(ang));
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    status = network_group_activated_event->signal();
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to signal network activation event");

    return activated_net_group_ptr;
}

const std::string &VDeviceActivatedNetworkGroup::get_network_group_name() const
{
    // network_group_name is same across all NGs
    return m_activated_network_groups[0]->get_network_group_name();
}

Expected<Buffer> VDeviceActivatedNetworkGroup::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    CHECK_AS_EXPECTED(1 == m_activated_network_groups.size(), HAILO_INVALID_OPERATION, "getting intermediate buffer is supported only over single device");
    return m_activated_network_groups[0]->get_intermediate_buffer(key);
}

hailo_status VDeviceActivatedNetworkGroup::set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset)
{
    for (auto &activated_network_group : m_activated_network_groups) {
        auto status = activated_network_group->set_keep_nn_config_during_reset(keep_nn_config_during_reset);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

VDeviceActivatedNetworkGroup::VDeviceActivatedNetworkGroup(std::vector<std::unique_ptr<ActivatedNetworkGroup>> &&activated_network_groups,
    std::map<std::string, std::shared_ptr<InputStream>> &input_streams, std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
    const hailo_activate_network_group_params_t &network_group_params, EventPtr network_group_activated_event, AccumulatorPtr deactivation_time_accumulator, hailo_status &status)
    : ActivatedNetworkGroupBase(network_group_params, input_streams, output_streams, std::move(network_group_activated_event), status),
        m_activated_network_groups(std::move(activated_network_groups)), m_should_reset_network_group(true), m_deactivation_time_accumulator(deactivation_time_accumulator)
{
}

VDeviceActivatedNetworkGroup::VDeviceActivatedNetworkGroup(VDeviceActivatedNetworkGroup &&other) noexcept :
    ActivatedNetworkGroupBase(std::move(other)),
    m_activated_network_groups(std::move(other.m_activated_network_groups)),
    m_should_reset_network_group(std::exchange(other.m_should_reset_network_group, false)),
    m_deactivation_time_accumulator(std::move(other.m_deactivation_time_accumulator))
{
}


Expected<std::shared_ptr<VDeviceNetworkGroup>> VDeviceNetworkGroup::create(std::vector<std::shared_ptr<ConfiguredNetworkGroup>> configured_network_group,
        NetworkGroupSchedulerWeakPtr network_group_scheduler)
{
    auto status = HAILO_UNINITIALIZED;
    std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> vdma_config_ngs;
    vdma_config_ngs.reserve(configured_network_group.size());
    for (auto &network_group : configured_network_group) {
        auto vdma_ng = std::dynamic_pointer_cast<VdmaConfigNetworkGroup>(network_group);
        assert(nullptr != vdma_ng);
        vdma_config_ngs.push_back(vdma_ng);
    }

    auto net_flow_ops_copy = vdma_config_ngs[0]->m_net_flow_ops;

    VDeviceNetworkGroup object(std::move(vdma_config_ngs), network_group_scheduler, std::move(net_flow_ops_copy), status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto obj_ptr = make_shared_nothrow<VDeviceNetworkGroup>(std::move(object));
    CHECK_NOT_NULL_AS_EXPECTED(obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return obj_ptr;
}

Expected<std::shared_ptr<VDeviceNetworkGroup>> VDeviceNetworkGroup::duplicate(std::shared_ptr<VDeviceNetworkGroup> other)
{
    auto status = HAILO_UNINITIALIZED;
    auto net_flow_ops_copy = other->m_net_flow_ops;

    VDeviceNetworkGroup object(other->m_configured_network_groups, other->m_network_group_scheduler, std::move(net_flow_ops_copy), status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto obj_ptr = make_shared_nothrow<VDeviceNetworkGroup>(std::move(object));
    CHECK_NOT_NULL_AS_EXPECTED(obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return obj_ptr;
}


VDeviceNetworkGroup::VDeviceNetworkGroup(std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> configured_network_group,
    NetworkGroupSchedulerWeakPtr network_group_scheduler, std::vector<std::shared_ptr<NetFlowElement>> &&net_flow_ops, hailo_status &status) :
        ConfiguredNetworkGroupBase(configured_network_group[0]->m_config_params,
            configured_network_group[0]->m_network_group_metadata, std::move(net_flow_ops), status),
        m_configured_network_groups(std::move(configured_network_group)),
        m_network_group_scheduler(network_group_scheduler),
        m_scheduler_handle(INVALID_NETWORK_GROUP_HANDLE),
        m_multiplexer_handle(0),
        m_multiplexer()
{}

Expected<hailo_stream_interface_t> VDeviceNetworkGroup::get_default_streams_interface()
{
    auto first_streams_interface = m_configured_network_groups[0]->get_default_streams_interface();
    CHECK_EXPECTED(first_streams_interface);
#ifndef NDEBUG
    // Check that all physical devices has the same interface
    for (auto &network_group : m_configured_network_groups) {
        auto iface = network_group->get_default_streams_interface();
        CHECK_EXPECTED(iface);
        CHECK_AS_EXPECTED(iface.value() == first_streams_interface.value(), HAILO_INTERNAL_FAILURE,
            "Not all default stream interfaces are the same");
    }
#endif
    return first_streams_interface;
}

hailo_status VDeviceNetworkGroup::create_vdevice_streams_from_config_params(std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle)
{
    // TODO - HRT-6931 - raise error on this case
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_configured_network_groups.size())) {
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

    for (const auto &input_stream : m_input_streams) {
        auto expected_queue_size = static_cast<InputStreamBase&>(*input_stream.second).get_buffer_frames_size();
        CHECK_EXPECTED_AS_STATUS(expected_queue_size);
        TRACE(CreateNetworkGroupInputStreamsTrace, "", name(), input_stream.first, (uint32_t)expected_queue_size.value());
    }
    for (const auto &output_stream : m_output_streams) {
        if (static_cast<OutputStreamBase&>(*output_stream.second).get_layer_info().format.order == hailo_format_order_t::HAILO_FORMAT_ORDER_HAILO_NMS) {
            continue;
        }
        auto expected_queue_size = static_cast<OutputStreamBase&>(*output_stream.second).get_buffer_frames_size();
        CHECK_EXPECTED_AS_STATUS(expected_queue_size);
        TRACE(CreateNetworkGroupOutputStreamsTrace, "", name(), output_stream.first, (uint32_t)expected_queue_size.value());
    }

    auto status = m_multiplexer->add_network_group_instance(m_multiplexer_handle, *this);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceNetworkGroup::create_input_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    CHECK(HailoRTCommon::is_vdma_stream_interface(stream_params.stream_interface), HAILO_INVALID_OPERATION,
        "Stream {} not supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);

    std::vector<std::reference_wrapper<VdmaInputStream>> low_level_streams;
    low_level_streams.reserve(m_configured_network_groups.size());
    for (auto &net_group : m_configured_network_groups) {
        auto stream = net_group->get_input_stream_by_name(stream_name);
        CHECK(stream, HAILO_INTERNAL_FAILURE);
        low_level_streams.emplace_back(dynamic_cast<VdmaInputStream&>(stream.release().get()));
    }
    auto input_stream = InputVDeviceBaseStream::create(std::move(low_level_streams), edge_layer.value(),
        scheduler_handle, m_network_group_activated_event, m_network_group_scheduler);
    CHECK_EXPECTED_AS_STATUS(input_stream);
    auto input_stream_wrapper = VDeviceInputStreamMultiplexerWrapper::create(input_stream.release(), edge_layer->network_name, multiplexer, scheduler_handle);
    CHECK_EXPECTED_AS_STATUS(input_stream_wrapper);
    m_input_streams.insert(make_pair(stream_name, input_stream_wrapper.release()));

    return HAILO_SUCCESS;
}

hailo_status VDeviceNetworkGroup::create_output_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t scheduler_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    CHECK(HailoRTCommon::is_vdma_stream_interface(stream_params.stream_interface), HAILO_INVALID_OPERATION,
        "Stream {} not supported on VDevice usage. {} has {} interface.", stream_name, stream_params.stream_interface);

    std::vector<std::reference_wrapper<VdmaOutputStream>> low_level_streams;
    low_level_streams.reserve(m_configured_network_groups.size());
    for (auto &net_group : m_configured_network_groups) {
        auto stream = net_group->get_output_stream_by_name(stream_name);
        CHECK(stream, HAILO_INTERNAL_FAILURE);
        low_level_streams.emplace_back(dynamic_cast<VdmaOutputStream&>(stream.release().get()));
    }
    auto output_stream = OutputVDeviceBaseStream::create(std::move(low_level_streams), edge_layer.value(),
        scheduler_handle, m_network_group_activated_event, m_network_group_scheduler);
    CHECK_EXPECTED_AS_STATUS(output_stream);
    auto output_stream_wrapper = VDeviceOutputStreamMultiplexerWrapper::create(output_stream.release(), edge_layer->network_name, multiplexer, scheduler_handle);
    CHECK_EXPECTED_AS_STATUS(output_stream_wrapper);
    m_output_streams.insert(make_pair(stream_name, output_stream_wrapper.release()));

    return HAILO_SUCCESS;
}

hailo_status VDeviceNetworkGroup::create_vdevice_streams_from_duplicate(std::shared_ptr<VDeviceNetworkGroup> other)
{
    // TODO - HRT-6931 - raise error on this case 
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_configured_network_groups.size())) {
        LOGGER__WARNING("Latency measurement is not supported on more than 1 physical device.");
    }

    m_multiplexer = other->m_multiplexer;
    m_multiplexer_handle = other->multiplexer_duplicates_count() + 1;

    for (auto &name_stream_pair : other->m_input_streams) {
        auto input_stream = static_cast<VDeviceInputStreamMultiplexerWrapper*>(name_stream_pair.second.get());
        auto copy = input_stream->clone(m_multiplexer_handle);
        CHECK_EXPECTED_AS_STATUS(copy);

        m_input_streams.insert(make_pair(name_stream_pair.first, copy.release()));
    }

    for (auto &name_stream_pair : other->m_output_streams) {
        auto output_stream = static_cast<VDeviceOutputStreamMultiplexerWrapper*>(name_stream_pair.second.get());
        auto copy = output_stream->clone(m_multiplexer_handle);
        CHECK_EXPECTED_AS_STATUS(copy);

        m_output_streams.insert(make_pair(name_stream_pair.first, copy.release()));
    }

    auto status = other->m_multiplexer->add_network_group_instance(m_multiplexer_handle, *this);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void VDeviceNetworkGroup::set_network_group_handle(scheduler_ng_handle_t handle)
{
    m_scheduler_handle = handle;
}

scheduler_ng_handle_t VDeviceNetworkGroup::network_group_handle() const
{
    return m_scheduler_handle;
}

hailo_status VDeviceNetworkGroup::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
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

hailo_status VDeviceNetworkGroup::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
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

Expected<std::shared_ptr<LatencyMetersMap>> VDeviceNetworkGroup::get_latency_meters()
{
    return m_configured_network_groups[0]->get_latency_meters();
}

Expected<std::shared_ptr<VdmaChannel>> VDeviceNetworkGroup::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    if (1 < m_configured_network_groups.size()) {
        LOGGER__ERROR("get_boundary_vdma_channel_by_stream_name function is not supported on more than 1 physical device.");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    return m_configured_network_groups[0]->get_boundary_vdma_channel_by_stream_name(stream_name);
}

Expected<std::vector<OutputVStream>> VDeviceNetworkGroup::create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params)
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

Expected<std::shared_ptr<VdmaConfigNetworkGroup>> VDeviceNetworkGroup::get_network_group_by_device_index(uint32_t device_index)
{
    CHECK_AS_EXPECTED(device_index <m_configured_network_groups.size(), HAILO_INVALID_ARGUMENT);
    auto vdma_config_network_group = m_configured_network_groups[device_index];
    return vdma_config_network_group;
}

} /* namespace hailort */
