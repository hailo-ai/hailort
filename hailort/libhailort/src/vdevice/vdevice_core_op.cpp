/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_core_op.cpp
 * @brief: VDeviceCoreOp implementation
 **/

#include "vdevice/vdevice_core_op.hpp"
#include "vdevice/vdevice_stream.hpp"
#include "vdevice/vdevice_stream_multiplexer_wrapper.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "utils/profiler/tracer_macros.hpp"


namespace hailort
{

Expected<std::unique_ptr<ActivatedNetworkGroup>> VDeviceActivatedCoreOp::create(
    std::vector<std::shared_ptr<CoreOp>> &core_ops,
    std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
    std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
    const hailo_activate_network_group_params_t &network_group_params,
    EventPtr core_op_activated_event, uint16_t dynamic_batch_size,
    AccumulatorPtr deactivation_time_accumulator,
    bool resume_pending_stream_transfers)
{
    auto status = HAILO_UNINITIALIZED;
    std::vector<std::unique_ptr<ActivatedNetworkGroup>> activated_network_groups;
    activated_network_groups.reserve(core_ops.size());
    for (auto core_op : core_ops) {
        auto ang = core_op->create_activated_network_group(network_group_params, dynamic_batch_size,
            resume_pending_stream_transfers);
        CHECK_EXPECTED(ang);
        activated_network_groups.emplace_back(ang.release());
    }
    auto ang = VDeviceActivatedCoreOp(std::move(activated_network_groups), input_streams, output_streams,
        network_group_params, core_op_activated_event, deactivation_time_accumulator, status);

    CHECK_SUCCESS_AS_EXPECTED(status);
    std::unique_ptr<ActivatedNetworkGroup> activated_net_group_ptr =
        make_unique_nothrow<VDeviceActivatedCoreOp>(std::move(ang));
    CHECK_AS_EXPECTED(nullptr != activated_net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    status = core_op_activated_event->signal();
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to signal network activation event");

    return activated_net_group_ptr;
}

const std::string &VDeviceActivatedCoreOp::get_network_group_name() const
{
    // network_group_name is same across all NGs
    return m_activated_network_groups[0]->get_network_group_name();
}

Expected<Buffer> VDeviceActivatedCoreOp::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    CHECK_AS_EXPECTED(1 == m_activated_network_groups.size(), HAILO_INVALID_OPERATION, "getting intermediate buffer is supported only over single device");
    return m_activated_network_groups[0]->get_intermediate_buffer(key);
}

hailo_status VDeviceActivatedCoreOp::set_keep_nn_config_during_reset(const bool keep_nn_config_during_reset)
{
    for (auto &activated_network_group : m_activated_network_groups) {
        auto status = activated_network_group->set_keep_nn_config_during_reset(keep_nn_config_during_reset);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

VDeviceActivatedCoreOp::VDeviceActivatedCoreOp(std::vector<std::unique_ptr<ActivatedNetworkGroup>> &&activated_network_groups,
    std::map<std::string, std::shared_ptr<InputStream>> &input_streams, std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
    const hailo_activate_network_group_params_t &network_group_params, EventPtr core_op_activated_event, AccumulatorPtr deactivation_time_accumulator, hailo_status &status)
    : ActivatedCoreOp(network_group_params, input_streams, output_streams, std::move(core_op_activated_event), status),
        m_activated_network_groups(std::move(activated_network_groups)), m_should_reset_core_op(true), m_deactivation_time_accumulator(deactivation_time_accumulator)
{
}

VDeviceActivatedCoreOp::VDeviceActivatedCoreOp(VDeviceActivatedCoreOp &&other) noexcept :
    ActivatedCoreOp(std::move(other)),
    m_activated_network_groups(std::move(other.m_activated_network_groups)),
    m_should_reset_core_op(std::exchange(other.m_should_reset_core_op, false)),
    m_deactivation_time_accumulator(std::move(other.m_deactivation_time_accumulator))
{
}


Expected<std::shared_ptr<VDeviceCoreOp>> VDeviceCoreOp::create(std::vector<std::shared_ptr<CoreOp>> core_ops,
        CoreOpsSchedulerWeakPtr core_ops_scheduler, const std::string &hef_hash)
{
    auto status = HAILO_UNINITIALIZED;

    VDeviceCoreOp object(std::move(core_ops), core_ops_scheduler, hef_hash, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto obj_ptr = make_shared_nothrow<VDeviceCoreOp>(std::move(object));
    CHECK_NOT_NULL_AS_EXPECTED(obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return obj_ptr;
}

Expected<std::shared_ptr<VDeviceCoreOp>> VDeviceCoreOp::duplicate(std::shared_ptr<VDeviceCoreOp> other)
{
    auto status = HAILO_UNINITIALIZED;
    auto copy = other->m_core_ops;

    VDeviceCoreOp object(std::move(copy), other->m_core_ops_scheduler, other->m_hef_hash, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto obj_ptr = make_shared_nothrow<VDeviceCoreOp>(std::move(object));
    CHECK_NOT_NULL_AS_EXPECTED(obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return obj_ptr;
}


VDeviceCoreOp::VDeviceCoreOp(std::vector<std::shared_ptr<CoreOp>> core_ops,
    CoreOpsSchedulerWeakPtr core_ops_scheduler, const std::string &hef_hash, hailo_status &status) :
        CoreOp(core_ops[0]->m_config_params, core_ops[0]->m_metadata, status),
        m_core_ops(std::move(core_ops)),
        m_core_ops_scheduler(core_ops_scheduler),
        m_scheduler_handle(INVALID_CORE_OP_HANDLE),
        m_multiplexer_handle(0),
        m_multiplexer(),
        m_hef_hash(hef_hash)
{}

Expected<hailo_stream_interface_t> VDeviceCoreOp::get_default_streams_interface()
{
    auto first_streams_interface = m_core_ops[0]->get_default_streams_interface();
    CHECK_EXPECTED(first_streams_interface);
#ifndef NDEBUG
    // Check that all physical devices has the same interface
    for (auto &core_op : m_core_ops) {
        auto iface = core_op->get_default_streams_interface();
        CHECK_EXPECTED(iface);
        CHECK_AS_EXPECTED(iface.value() == first_streams_interface.value(), HAILO_INTERNAL_FAILURE,
            "Not all default stream interfaces are the same");
    }
#endif
    return first_streams_interface;
}

hailo_status VDeviceCoreOp::create_vdevice_streams_from_config_params(std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_core_op_handle_t scheduler_handle)
{
    // TODO - HRT-6931 - raise error on this case
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_core_ops.size())) {
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
        if (HAILO_STREAM_INTERFACE_ETH == static_cast<InputStreamBase&>(*input_stream.second).get_interface()) {
            continue;
        }
        auto expected_queue_size = static_cast<InputStreamBase&>(*input_stream.second).get_buffer_frames_size();
        CHECK_EXPECTED_AS_STATUS(expected_queue_size);
        TRACE(CreateCoreOpInputStreamsTrace, "", name(), input_stream.first, (uint32_t)expected_queue_size.value());
    }
    for (const auto &output_stream : m_output_streams) {
        if ((hailo_format_order_t::HAILO_FORMAT_ORDER_HAILO_NMS == (static_cast<OutputStreamBase&>(*output_stream.second).get_layer_info().format.order)) ||
            (HAILO_STREAM_INTERFACE_ETH == static_cast<OutputStreamBase&>(*output_stream.second).get_interface())) {
            continue;
        }
        auto expected_queue_size = static_cast<OutputStreamBase&>(*output_stream.second).get_buffer_frames_size();
        CHECK_EXPECTED_AS_STATUS(expected_queue_size);
        TRACE(CreateCoreOpOutputStreamsTrace, "", name(), output_stream.first, (uint32_t)expected_queue_size.value());
    }

    auto status = m_multiplexer->add_core_op_instance(m_multiplexer_handle, *this);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::create_input_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_core_op_handle_t scheduler_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    if (HailoRTCommon::is_vdma_stream_interface(stream_params.stream_interface)){
        std::vector<std::reference_wrapper<VdmaInputStream>> low_level_streams;
        low_level_streams.reserve(m_core_ops.size());
        for (auto &core_op : m_core_ops) {
            auto stream = core_op->get_input_stream_by_name(stream_name);
            CHECK(stream, HAILO_INTERNAL_FAILURE);
            low_level_streams.emplace_back(dynamic_cast<VdmaInputStream&>(stream.release().get()));
        }
        auto input_stream = InputVDeviceBaseStream::create(std::move(low_level_streams), edge_layer.value(),
            scheduler_handle, m_core_op_activated_event, m_core_ops_scheduler);
        CHECK_EXPECTED_AS_STATUS(input_stream);
        auto input_stream_wrapper = VDeviceInputStreamMultiplexerWrapper::create(input_stream.release(), edge_layer->network_name, multiplexer, scheduler_handle);
        CHECK_EXPECTED_AS_STATUS(input_stream_wrapper);
        m_input_streams.insert(make_pair(stream_name, input_stream_wrapper.release()));
    } else {
        assert(1 == m_core_ops.size());
        auto stream = m_core_ops[0]->get_input_stream_by_name(stream_name);
        CHECK(stream, HAILO_INTERNAL_FAILURE);
        assert(1 == m_core_ops.size());
        assert(contains(m_core_ops[0]->m_input_streams, stream_name));
        m_input_streams.insert(make_pair(stream_name, m_core_ops[0]->m_input_streams.at(stream_name)));
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::create_output_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_core_op_handle_t scheduler_handle)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    if (HailoRTCommon::is_vdma_stream_interface(stream_params.stream_interface)) {
        std::vector<std::reference_wrapper<VdmaOutputStream>> low_level_streams;
        low_level_streams.reserve(m_core_ops.size());
        for (auto &core_op : m_core_ops) {
            auto stream = core_op->get_output_stream_by_name(stream_name);
            CHECK(stream, HAILO_INTERNAL_FAILURE);
            low_level_streams.emplace_back(dynamic_cast<VdmaOutputStream&>(stream.release().get()));
        }
        auto output_stream = OutputVDeviceBaseStream::create(std::move(low_level_streams), edge_layer.value(),
            scheduler_handle, m_core_op_activated_event, m_core_ops_scheduler);
        CHECK_EXPECTED_AS_STATUS(output_stream);
        auto output_stream_wrapper = VDeviceOutputStreamMultiplexerWrapper::create(output_stream.release(), edge_layer->network_name, multiplexer, scheduler_handle);
        CHECK_EXPECTED_AS_STATUS(output_stream_wrapper);
        m_output_streams.insert(make_pair(stream_name, output_stream_wrapper.release()));
    } else {
        assert(1 == m_core_ops.size());
        assert(contains(m_core_ops[0]->m_output_streams, stream_name));
        m_output_streams.insert(make_pair(stream_name, m_core_ops[0]->m_output_streams.at(stream_name)));
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::create_vdevice_streams_from_duplicate(std::shared_ptr<VDeviceCoreOp> other)
{
    // TODO - HRT-6931 - raise error on this case 
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_core_ops.size())) {
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

    auto status = other->m_multiplexer->add_core_op_instance(m_multiplexer_handle, *this);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void VDeviceCoreOp::set_core_op_handle(scheduler_core_op_handle_t handle)
{
    m_scheduler_handle = handle;
}

scheduler_core_op_handle_t VDeviceCoreOp::core_op_handle() const
{
    return m_scheduler_handle;
}

bool VDeviceCoreOp::is_scheduled() const
{
    return !m_core_ops_scheduler.expired();
};

hailo_status VDeviceCoreOp::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INVALID_OPERATION,
        "Cannot set scheduler timeout for core-op {}, as it is configured on a vdevice which does not have scheduling enabled", name());
    if (network_name != HailoRTDefaults::get_network_name(name())) {
        CHECK(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting scheduler timeout for a specific network is currently not supported");
    }
    auto status = core_ops_scheduler->set_timeout(m_scheduler_handle, timeout, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INVALID_OPERATION,
        "Cannot set scheduler threshold for core-op {}, as it is configured on a vdevice which does not have scheduling enabled", name());
    if (network_name != HailoRTDefaults::get_network_name(name())) {
        CHECK(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting scheduler threshold for a specific network is currently not supported");
    }
    auto status = core_ops_scheduler->set_threshold(m_scheduler_handle, threshold, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::set_scheduler_priority(uint8_t priority, const std::string &network_name)
{
    auto core_ops_scheduler = m_core_ops_scheduler.lock();
    CHECK(core_ops_scheduler, HAILO_INVALID_OPERATION,
        "Cannot set scheduler priority for core-op {}, as it is configured on a vdevice which does not have scheduling enabled", name());
    if (network_name != HailoRTDefaults::get_network_name(name())) {
        CHECK(network_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting scheduler priority for a specific network is currently not supported");
    }
    auto status = core_ops_scheduler->set_priority(m_scheduler_handle, priority, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<LatencyMetersMap>> VDeviceCoreOp::get_latency_meters()
{
    return m_core_ops[0]->get_latency_meters();
}

Expected<vdma::BoundaryChannelPtr> VDeviceCoreOp::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    CHECK_AS_EXPECTED(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_boundary_vdma_channel_by_stream_name function is not supported on more than 1 physical device.");

    return m_core_ops[0]->get_boundary_vdma_channel_by_stream_name(stream_name);
}

void VDeviceCoreOp::set_vstreams_multiplexer_callbacks(std::vector<OutputVStream> &output_vstreams)
{
    if (nullptr == m_multiplexer) {
        return;
    }

    m_multiplexer->set_output_vstreams_names(m_multiplexer_handle, output_vstreams);

    for (auto &vstream : output_vstreams) {
        static_cast<OutputVStreamImpl&>(*vstream.m_vstream).set_on_vstream_cant_read_callback([this, name = vstream.name()] () {
            m_multiplexer->set_can_output_vstream_read(m_multiplexer_handle, name, false);
        });
        static_cast<OutputVStreamImpl&>(*vstream.m_vstream).set_on_vstream_can_read_callback([this, name = vstream.name()] () {
            m_multiplexer->set_can_output_vstream_read(m_multiplexer_handle, name, true);
        });
    }
}

Expected<std::shared_ptr<VdmaConfigCoreOp>> VDeviceCoreOp::get_core_op_by_device_index(uint32_t device_index)
{
    CHECK_AS_EXPECTED(device_index < m_core_ops.size(), HAILO_INVALID_ARGUMENT);
    auto core_op = std::dynamic_pointer_cast<VdmaConfigCoreOp>(m_core_ops[device_index]);
    CHECK_NOT_NULL_AS_EXPECTED(core_op, HAILO_INTERNAL_FAILURE);
    return core_op;
}

Expected<std::unique_ptr<ActivatedNetworkGroup>> VDeviceCoreOp::create_activated_network_group(
    const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size,
    bool resume_pending_stream_transfers)
{
    auto start_time = std::chrono::steady_clock::now();

    CHECK_AS_EXPECTED(!m_core_ops_scheduler.lock(), HAILO_INVALID_OPERATION,
        "Manually activating a core-op is not allowed when the core-op scheduler is active!");

    auto res = VDeviceActivatedCoreOp::create(m_core_ops, m_input_streams, m_output_streams,
        network_group_params, m_core_op_activated_event, dynamic_batch_size, m_deactivation_time_accumulator,
        resume_pending_stream_transfers);
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    CHECK_EXPECTED(res);

    LOGGER__INFO("Activating {} on VDevice took {} milliseconds. Note that the function is asynchronous and"
        " thus the network is not fully activated yet.", name(), elapsed_time_ms);
    m_activation_time_accumulator->add_data_point(elapsed_time_ms);

    return res;
}

} /* namespace hailort */
