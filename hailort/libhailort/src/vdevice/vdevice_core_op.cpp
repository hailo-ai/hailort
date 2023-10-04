/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_core_op.cpp
 * @brief: VDeviceCoreOp implementation
 **/

#include "vdevice/vdevice_core_op.hpp"
#include "vdevice/scheduler/scheduled_stream.hpp"
#include "vdevice/vdevice_native_stream.hpp"
#include "vdevice/vdevice_stream_multiplexer_wrapper.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"

#define INVALID_BATCH_SIZE (-1)


namespace hailort
{

Expected<std::shared_ptr<VDeviceCoreOp>> VDeviceCoreOp::create(ActiveCoreOpHolder &active_core_op_holder,
    const ConfigureNetworkParams &configure_params,
    const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
    CoreOpsSchedulerWeakPtr core_ops_scheduler, vdevice_core_op_handle_t core_op_handle,
    std::shared_ptr<PipelineMultiplexer> multiplexer,
    const std::string &hef_hash)
{
    auto status = HAILO_UNINITIALIZED;

    for (auto &core_op : core_ops)
    {
        core_op.second->set_vdevice_core_op_handle(core_op_handle);
        for (auto &stream : core_op.second->get_input_streams())
        {
            auto &native_stream = static_cast<VDeviceNativeInputStream&>(stream.get());
            native_stream.set_vdevice_core_op_handle(core_op_handle);
        }
    }

    VDeviceCoreOp object(active_core_op_holder, configure_params, std::move(core_ops), core_ops_scheduler,
        core_op_handle, multiplexer, hef_hash, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    int batch_size = INVALID_BATCH_SIZE;
    bool batch_size_equals = std::all_of(configure_params.network_params_by_name.begin(),
        configure_params.network_params_by_name.end(), [&](std::pair<std::string, hailo_network_parameters_t> n_param_map) {
        return n_param_map.second.batch_size == configure_params.network_params_by_name.begin()->second.batch_size;
    });
    if (batch_size_equals) {
        batch_size = configure_params.network_params_by_name.begin()->second.batch_size;
    }

    // TODO HRT-11373: remove is_nms from monitor
    TRACE(AddCoreOpTrace, "", object.name(), DEFAULT_SCHEDULER_TIMEOUT.count(), DEFAULT_SCHEDULER_MIN_THRESHOLD,
        core_op_handle, object.is_nms(), batch_size);
    status = object.create_vdevice_streams_from_config_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto obj_ptr = make_shared_nothrow<VDeviceCoreOp>(std::move(object));
    CHECK_NOT_NULL_AS_EXPECTED(obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return obj_ptr;
}

Expected<std::shared_ptr<VDeviceCoreOp>> VDeviceCoreOp::duplicate(std::shared_ptr<VDeviceCoreOp> other,
    const ConfigureNetworkParams &configure_params)
{
    auto status = HAILO_UNINITIALIZED;
    auto copy = other->m_core_ops;

    VDeviceCoreOp object(other->m_active_core_op_holder, configure_params, std::move(copy), other->m_core_ops_scheduler,
        other->m_core_op_handle, other->m_multiplexer, other->m_hef_hash, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = object.create_vdevice_streams_from_duplicate(other);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto obj_ptr = make_shared_nothrow<VDeviceCoreOp>(std::move(object));
    CHECK_NOT_NULL_AS_EXPECTED(obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return obj_ptr;
}


VDeviceCoreOp::VDeviceCoreOp(ActiveCoreOpHolder &active_core_op_holder,
    const ConfigureNetworkParams &configure_params,
    const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
    CoreOpsSchedulerWeakPtr core_ops_scheduler, vdevice_core_op_handle_t core_op_handle,
    std::shared_ptr<PipelineMultiplexer> multiplexer, const std::string &hef_hash, hailo_status &status) :
        CoreOp(configure_params, core_ops.begin()->second->m_metadata, active_core_op_holder, status),
        m_core_ops(std::move(core_ops)),
        m_core_ops_scheduler(core_ops_scheduler),
        m_core_op_handle(core_op_handle),
        m_multiplexer(multiplexer),
        m_multiplexer_handle(0),
        m_hef_hash(hef_hash)
{}

Expected<hailo_stream_interface_t> VDeviceCoreOp::get_default_streams_interface()
{
    auto first_streams_interface = m_core_ops.begin()->second->get_default_streams_interface();
    CHECK_EXPECTED(first_streams_interface);
#ifndef NDEBUG
    // Check that all physical devices has the same interface
    for (const auto &pair : m_core_ops) {
        auto &core_op = pair.second;
        auto iface = core_op->get_default_streams_interface();
        CHECK_EXPECTED(iface);
        CHECK_AS_EXPECTED(iface.value() == first_streams_interface.value(), HAILO_INTERNAL_FAILURE,
            "Not all default stream interfaces are the same");
    }
#endif
    return first_streams_interface;
}

hailo_status VDeviceCoreOp::create_vdevice_streams_from_config_params()
{
    // TODO - HRT-6931 - raise error on this case
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_core_ops.size())) {
        LOGGER__WARNING("Latency measurement is not supported on more than 1 physical device.");
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

    for (const auto &input_stream : m_input_streams) {
        if (HAILO_STREAM_INTERFACE_ETH == input_stream.second->get_interface()) {
            continue;
        }
        auto expected_queue_size = input_stream.second->get_buffer_frames_size();
        CHECK_EXPECTED_AS_STATUS(expected_queue_size);
    }
    for (const auto &output_stream : m_output_streams) {
        if (HAILO_STREAM_INTERFACE_ETH == output_stream.second->get_interface()) {
            continue;
        }
        auto expected_queue_size = output_stream.second->get_buffer_frames_size();
        CHECK_EXPECTED_AS_STATUS(expected_queue_size);
    }

    if (m_multiplexer) {
        auto status = m_multiplexer->add_core_op_instance(m_multiplexer_handle, *this);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::create_input_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    std::map<device_id_t, std::reference_wrapper<InputStreamBase>> low_level_streams;
    for (const auto &pair : m_core_ops) {
        auto &device_id = pair.first;
        auto &core_op = pair.second;
        auto stream = core_op->get_input_stream_by_name(stream_name);
        CHECK(stream, HAILO_INTERNAL_FAILURE);
        TRACE(CreateCoreOpInputStreamsTrace, device_id, name(), stream_name, (uint32_t)stream->get().get_buffer_frames_size().value(),
            core_op->vdevice_core_op_handle());
        low_level_streams.emplace(device_id, stream.release());
    }

    std::shared_ptr<InputStreamBase> input_stream = nullptr;

    if (m_core_ops_scheduler.lock()) {
        auto scheduled_stream = ScheduledInputStream::create(std::move(low_level_streams),
            edge_layer.value(), m_core_op_handle, m_core_ops_scheduler, m_core_op_activated_event);
        CHECK_EXPECTED_AS_STATUS(scheduled_stream);

        if (m_multiplexer) {
            auto multiplexer_stream = VDeviceInputStreamMultiplexerWrapper::create(scheduled_stream.release(),
                edge_layer->network_name, m_multiplexer);
            CHECK_EXPECTED_AS_STATUS(multiplexer_stream);

            input_stream = multiplexer_stream.release();
        } else {
            input_stream = scheduled_stream.release();
        }

    } else {
        auto max_batch_size = get_stream_batch_size(stream_name);
        CHECK_EXPECTED_AS_STATUS(max_batch_size);

        auto native_stream = VDeviceNativeInputStream::create(std::move(low_level_streams),
            m_core_op_activated_event, edge_layer.value(), max_batch_size.release(), m_core_op_handle);
        CHECK_EXPECTED_AS_STATUS(native_stream);

        input_stream = native_stream.release();
    }

    auto status = add_input_stream(std::move(input_stream), stream_params);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::create_output_vdevice_stream_from_config_params(const hailo_stream_parameters_t &stream_params,
    const std::string &stream_name)
{
    auto edge_layer = get_layer_info(stream_name);
    CHECK_EXPECTED_AS_STATUS(edge_layer);

    std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> low_level_streams;
    for (const auto &pair : m_core_ops) {
        auto &device_id = pair.first;
        auto &core_op = pair.second;
        auto stream = core_op->get_output_stream_by_name(stream_name);
        CHECK(stream, HAILO_INTERNAL_FAILURE);
        TRACE(CreateCoreOpOutputStreamsTrace, device_id, name(), stream_name, (uint32_t)stream->get().get_buffer_frames_size().value(),
            core_op->vdevice_core_op_handle());
        low_level_streams.emplace(device_id, stream.release());
    }

    std::shared_ptr<OutputStreamBase> output_stream = nullptr;

    if (m_core_ops_scheduler.lock()) {
        auto scheduled_stream = ScheduledOutputStream::create(std::move(low_level_streams), m_core_op_handle,
            edge_layer.value(), m_core_op_activated_event, m_core_ops_scheduler);
        CHECK_EXPECTED_AS_STATUS(scheduled_stream);

        if (m_multiplexer) {
            auto multiplexer_stream = VDeviceOutputStreamMultiplexerWrapper::create(scheduled_stream.release(),
                edge_layer->network_name, m_multiplexer);
            CHECK_EXPECTED_AS_STATUS(multiplexer_stream);

            output_stream = multiplexer_stream.release();
        } else {
            output_stream = scheduled_stream.release();
        }

    } else {
        auto max_batch_size = get_stream_batch_size(stream_name);
        CHECK_EXPECTED_AS_STATUS(max_batch_size);

        auto native_stream = VDeviceNativeOutputStream::create(std::move(low_level_streams),
            m_core_op_activated_event, edge_layer.value(), max_batch_size.release(), m_core_op_handle);
        CHECK_EXPECTED_AS_STATUS(native_stream);

        output_stream = native_stream.release();
    }

    auto status = add_output_stream(std::move(output_stream), stream_params);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceCoreOp::create_vdevice_streams_from_duplicate(std::shared_ptr<VDeviceCoreOp> other)
{
    // TODO - HRT-6931 - raise error on this case
    if (((m_config_params.latency & HAILO_LATENCY_MEASURE) == HAILO_LATENCY_MEASURE) && (1 < m_core_ops.size())) {
        LOGGER__WARNING("Latency measurement is not supported on more than 1 physical device.");
    }

    assert(other->m_multiplexer != nullptr);
    m_multiplexer_handle = other->multiplexer_duplicates_count() + 1;

    for (const auto &stream_parameters_pair : m_config_params.stream_params_by_name) {
        switch (stream_parameters_pair.second.direction) {
        case HAILO_H2D_STREAM:
        {
            auto other_stream = other->get_input_stream_by_name(stream_parameters_pair.first);
            CHECK_EXPECTED_AS_STATUS(other_stream);
            auto &other_stream_wrapper = dynamic_cast<VDeviceInputStreamMultiplexerWrapper&>(other_stream->get());

            auto copy = other_stream_wrapper.clone(m_multiplexer_handle);
            CHECK_EXPECTED_AS_STATUS(copy);

            auto status = add_input_stream(copy.release(), stream_parameters_pair.second);
            CHECK_SUCCESS(status);
            break;
        }
        case HAILO_D2H_STREAM:
        {
            auto other_stream = other->get_output_stream_by_name(stream_parameters_pair.first);
            CHECK_EXPECTED_AS_STATUS(other_stream);
            auto &other_stream_wrapper = dynamic_cast<VDeviceOutputStreamMultiplexerWrapper&>(other_stream->get());

            auto copy = other_stream_wrapper.clone(m_multiplexer_handle);
            CHECK_EXPECTED_AS_STATUS(copy);

            auto status = add_output_stream(copy.release(), stream_parameters_pair.second);
            CHECK_SUCCESS(status);
            break;
        }
        default:
            LOGGER__ERROR("stream name {} direction is invalid.", stream_parameters_pair.first);
            return HAILO_INVALID_ARGUMENT;
        }
    }

    auto status = other->m_multiplexer->add_core_op_instance(m_multiplexer_handle, *this);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

vdevice_core_op_handle_t VDeviceCoreOp::core_op_handle() const
{
    return m_core_op_handle;
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
    auto status = core_ops_scheduler->set_timeout(m_core_op_handle, timeout, network_name);
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
    auto status = core_ops_scheduler->set_threshold(m_core_op_handle, threshold, network_name);
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
    auto status = core_ops_scheduler->set_priority(m_core_op_handle, priority, network_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<LatencyMetersMap>> VDeviceCoreOp::get_latency_meters()
{
    return m_core_ops.begin()->second->get_latency_meters();
}

Expected<vdma::BoundaryChannelPtr> VDeviceCoreOp::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    CHECK_AS_EXPECTED(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_boundary_vdma_channel_by_stream_name function is not supported on more than 1 physical device.");

    return m_core_ops.begin()->second->get_boundary_vdma_channel_by_stream_name(stream_name);
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

hailo_status VDeviceCoreOp::activate_impl(uint16_t dynamic_batch_size)
{
    assert(!m_core_ops_scheduler.lock());

    // Activate all physical device core ops.
    for (const auto &pair : m_core_ops) {
        auto &core_op = pair.second;
        auto status = core_op->activate(dynamic_batch_size);
        CHECK_SUCCESS(status);
    }

    // Activate low level streams
    return activate_low_level_streams();
}

hailo_status VDeviceCoreOp::deactivate_impl()
{
    auto status = HAILO_SUCCESS; // Success oriented

    auto deactivate_status = deactivate_low_level_streams();
    if (HAILO_SUCCESS != deactivate_status) {
        LOGGER__ERROR("Failed to deactivate low level streams with {}", deactivate_status);
        status = deactivate_status; // continue on failure
    }

    for (const auto &pair : m_core_ops) {
        auto &core_op = pair.second;
        deactivate_status = core_op->deactivate();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to deactivate low level streams with {}", deactivate_status);
            status = deactivate_status; // continue on failure

        }
    }

    return status;
}

Expected<std::shared_ptr<VdmaConfigCoreOp>> VDeviceCoreOp::get_core_op_by_device_id(const device_id_t &device_id)
{
    CHECK_AS_EXPECTED(m_core_ops.count(device_id), HAILO_INVALID_ARGUMENT);
    auto core_op = std::dynamic_pointer_cast<VdmaConfigCoreOp>(m_core_ops[device_id]);
    CHECK_NOT_NULL_AS_EXPECTED(core_op, HAILO_INTERNAL_FAILURE);
    return core_op;
}

Expected<HwInferResults> VDeviceCoreOp::run_hw_infer_estimator()
{
    CHECK_AS_EXPECTED(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "run_hw_infer_estimator function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->run_hw_infer_estimator();
}

Expected<Buffer> VDeviceCoreOp::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    CHECK_AS_EXPECTED(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_intermediate_buffer function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->get_intermediate_buffer(key);
}

} /* namespace hailort */
