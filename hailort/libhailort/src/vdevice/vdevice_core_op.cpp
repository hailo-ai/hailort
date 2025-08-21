/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_core_op.cpp
 * @brief: VDeviceCoreOp implementation
 **/

#include "vdevice/vdevice_core_op.hpp"
#include "vdevice/scheduler/scheduled_stream.hpp"
#include "vdevice/vdevice_native_stream.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "common/utils.hpp"

#define INVALID_BATCH_SIZE (-1)


namespace hailort
{

Expected<std::shared_ptr<VDeviceCoreOp>> VDeviceCoreOp::create(VDevice &vdevice,
    ActiveCoreOpHolder &active_core_op_holder,
    const ConfigureNetworkParams &configure_params,
    const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
    CoreOpsSchedulerWeakPtr core_ops_scheduler, vdevice_core_op_handle_t core_op_handle,
    const std::string &hef_hash)
{

    for (auto &core_op : core_ops) {
        core_op.second->set_vdevice_core_op_handle(core_op_handle);
        for (auto &stream : core_op.second->get_input_streams()) {
            auto &stream_base = dynamic_cast<InputStreamBase&>(stream.get());
            stream_base.set_vdevice_core_op_handle(core_op_handle);
        }
        for (auto &stream : core_op.second->get_output_streams()) {
            auto &stream_base = dynamic_cast<OutputStreamBase&>(stream.get());
            stream_base.set_vdevice_core_op_handle(core_op_handle);
        }
    }

    size_t queue_size = 0;
    auto iface = core_ops.begin()->second->get_default_streams_interface();
    CHECK_EXPECTED(iface);
    if ((iface.value() != HAILO_STREAM_INTERFACE_ETH) && (iface.value() != HAILO_STREAM_INTERFACE_MIPI)) {
        auto per_device_queue_size = core_ops.begin()->second->infer_queue_size();
        CHECK_EXPECTED(per_device_queue_size);
        queue_size = *per_device_queue_size * core_ops.size();
    }

    auto status = HAILO_UNINITIALIZED;
    auto vdevice_core_op = make_shared_nothrow<VDeviceCoreOp>(vdevice, active_core_op_holder, configure_params,
        std::move(core_ops), core_ops_scheduler, core_op_handle, hef_hash, queue_size, status);
    CHECK_NOT_NULL_AS_EXPECTED(vdevice_core_op, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = vdevice_core_op->create_vdevice_streams_from_config_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = vdevice_core_op->add_to_trace();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return vdevice_core_op;
}

Expected<std::shared_ptr<VDeviceCoreOp>> VDeviceCoreOp::duplicate(std::shared_ptr<VDeviceCoreOp> other,
    const ConfigureNetworkParams &configure_params)
{
    auto copy = other->m_core_ops;

    // If m_infer_requests_accumulator does not exists (if the scheduler is not in use), we don't need queue size, so 
    // we pass 0.
    const auto queue_size = other->m_infer_requests_accumulator ?
        other->m_infer_requests_accumulator->queue_size() : 0;

    auto status = HAILO_UNINITIALIZED;
    auto vdevice_core_op = make_shared_nothrow<VDeviceCoreOp>(other->m_vdevice, other->m_active_core_op_holder,
        configure_params, std::move(copy), other->m_core_ops_scheduler, other->m_core_op_handle,
        other->m_hef_hash, queue_size, status);
    CHECK_NOT_NULL_AS_EXPECTED(vdevice_core_op, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = vdevice_core_op->create_vdevice_streams_from_config_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return vdevice_core_op;
}

VDeviceCoreOp::~VDeviceCoreOp()
{
    (void)shutdown_impl();
}

VDeviceCoreOp::VDeviceCoreOp(VDevice &vdevice,
    ActiveCoreOpHolder &active_core_op_holder,
    const ConfigureNetworkParams &configure_params,
    const std::map<device_id_t, std::shared_ptr<CoreOp>> &core_ops,
    CoreOpsSchedulerWeakPtr core_ops_scheduler, vdevice_core_op_handle_t core_op_handle,
    const std::string &hef_hash, size_t max_queue_size,
    hailo_status &status) :
        CoreOp(configure_params, core_ops.begin()->second->m_metadata, active_core_op_holder, status, !core_ops_scheduler.expired()),
        m_vdevice(vdevice),
        m_core_ops(std::move(core_ops)),
        m_core_ops_scheduler(core_ops_scheduler),
        m_core_op_handle(core_op_handle),
        m_hef_hash(hef_hash),
        m_infer_requests_accumulator(nullptr)
{
    if (HAILO_SUCCESS != status) {
        // Failure from base class
        return;
    }

    if (m_core_ops_scheduler.lock() && (max_queue_size > 0)) {
        const auto streams_count = m_config_params.stream_params_by_name.size();
        auto infer_request_accumulator =
            make_shared_nothrow<InferRequestAccumulator>(streams_count, max_queue_size,
                [this](InferRequest &&infer_request) {
                    auto scheduler = m_core_ops_scheduler.lock();
                    if (!scheduler) {
                        LOGGER__ERROR("Frame accumulator is supported only when scheduler is enabled");
                        return;
                    }
                    auto status = scheduler->enqueue_infer_request(m_core_op_handle, std::move(infer_request));
                    if (HAILO_SUCCESS != status) {
                        LOGGER__ERROR("Failed to enqueue infer request with status={}", status);
                    }
                });
        if (!infer_request_accumulator) {
            LOGGER__ERROR("Failed to allocated infer request accumulator");
            status = HAILO_OUT_OF_HOST_MEMORY;
            return;
        }

        m_infer_requests_accumulator = infer_request_accumulator;
    }

    if (has_caches() && is_scheduled()) {
        const auto configured_batch_size = get_smallest_configured_batch_size(configure_params);
        if (configured_batch_size > 1) {
            LOGGER__ERROR("Caches are not supported with the scheduler when using a batch size greater than 1 (received {})",
                configured_batch_size);
            status = HAILO_INVALID_OPERATION;
            return;
        }
    }

    status = HAILO_SUCCESS;
}

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

        low_level_streams.emplace(device_id, stream.release());
    }

    std::shared_ptr<InputStreamBase> input_stream = nullptr;

    if (m_core_ops_scheduler.lock()) {
        assert(m_infer_requests_accumulator);
        auto scheduled_stream = ScheduledInputStream::create(m_vdevice, std::move(low_level_streams),
            edge_layer.value(), m_core_op_handle, m_core_op_activated_event, m_infer_requests_accumulator);
        CHECK_EXPECTED_AS_STATUS(scheduled_stream);

        input_stream = scheduled_stream.release();
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
        low_level_streams.emplace(device_id, stream.release());
    }

    std::shared_ptr<OutputStreamBase> output_stream = nullptr;

    if (m_core_ops_scheduler.lock()) {
        assert(m_infer_requests_accumulator);
        auto scheduled_stream = ScheduledOutputStream::create(m_vdevice, std::move(low_level_streams),
            m_core_op_handle, edge_layer.value(), m_core_op_activated_event, m_infer_requests_accumulator);
        CHECK_EXPECTED_AS_STATUS(scheduled_stream);

        output_stream = scheduled_stream.release();
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

hailo_status VDeviceCoreOp::shutdown()
{
    return shutdown_impl();
}

hailo_status VDeviceCoreOp::shutdown_impl()
{
    hailo_status status = HAILO_SUCCESS; // Success oriented

    if (m_is_shutdown.exchange(true)) {
        return HAILO_SUCCESS;
    }

    auto abort_status = abort_low_level_streams();
    if (HAILO_SUCCESS != abort_status) {
        LOGGER__ERROR("Failed abort low level streams {}", abort_status);
        status = abort_status;
    }

    if (m_core_ops_scheduler.lock()) {

        auto deactivate_streams_status = deactivate_low_level_streams();
        if (HAILO_SUCCESS != deactivate_streams_status) {
            status = deactivate_streams_status;
            // continue
        }

        m_core_ops_scheduler.lock()->remove_core_op(m_core_op_handle);

        assert(m_infer_requests_accumulator);
        TRY(auto queue_size, infer_queue_size());

        const auto timeout = DEFAULT_TRANSFER_TIMEOUT * queue_size;
        auto accumulator_shutdown_status = m_infer_requests_accumulator->shutdown(timeout);
        if (HAILO_SUCCESS != accumulator_shutdown_status) {
            status = accumulator_shutdown_status;
            // continue
        }

        // Here, we can shutdown the VdmaConfigCoreOps if shutdown was called on last instance. We don't do it for now,
        // since after shutdown the user can create another instance of this core op (so we need the resources
        // available).

    } else {
        for (auto &core_op : m_core_ops) {
            auto shutdown_status = core_op.second->shutdown();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed shutdown core op for device {}", core_op.first);
                status = shutdown_status; // continue on failure
            }
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

Expected<size_t> VDeviceCoreOp::get_infer_queue_size_per_device() const
{
    return m_core_ops.begin()->second->infer_queue_size();
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

Expected<uint32_t> VDeviceCoreOp::get_cache_length() const
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_cache_length function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->get_cache_length();
}

Expected<uint32_t> VDeviceCoreOp::get_cache_read_length() const
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_cache_read_length function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->get_cache_read_length();
}

Expected<uint32_t> VDeviceCoreOp::get_cache_write_length() const
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_cache_write_length function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->get_cache_write_length();
}

Expected<uint32_t> VDeviceCoreOp::get_cache_entry_size(uint32_t cache_id) const
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_cache_entry_size function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->get_cache_entry_size(cache_id);
}

bool VDeviceCoreOp::has_caches() const
{
    for (const auto &core_op : m_core_ops) {
        if (core_op.second->has_caches()) {
            return true;
        }
    }

    return false;
}

hailo_status VDeviceCoreOp::init_cache(uint32_t read_offset)
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "init_cache function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->init_cache(read_offset);
}

hailo_status VDeviceCoreOp::update_cache_offset(int32_t offset_delta_entries)
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "update_cache_offset function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->update_cache_offset(offset_delta_entries);
}

Expected<std::vector<uint32_t>> VDeviceCoreOp::get_cache_ids() const
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "get_cache_ids function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->get_cache_ids();
}

Expected<Buffer> VDeviceCoreOp::read_cache_buffer(uint32_t cache_id)
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "read_cache_buffer function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->read_cache_buffer(cache_id);
}

hailo_status VDeviceCoreOp::write_cache_buffer(uint32_t cache_id, MemoryView buffer)
{
    CHECK(1 == m_core_ops.size(), HAILO_INVALID_OPERATION,
        "write_cache_buffer function is not supported on more than 1 physical device.");
    return m_core_ops.begin()->second->write_cache_buffer(cache_id, buffer);
}

hailo_status VDeviceCoreOp::add_to_trace()
{
    const auto batch_size = get_stream_batch_size(m_config_params.stream_params_by_name.begin()->first);
    CHECK_EXPECTED_AS_STATUS(batch_size);

    TRACE(AddCoreOpTrace, name(), DEFAULT_SCHEDULER_TIMEOUT.count(), DEFAULT_SCHEDULER_MIN_THRESHOLD,
        m_core_op_handle, *batch_size);

    const auto stream_interface = get_default_streams_interface();
    CHECK_EXPECTED_AS_STATUS(stream_interface);

    if (*stream_interface != HAILO_STREAM_INTERFACE_ETH) {
        for (const auto &core_op : m_core_ops) {
            auto queue_size_exp = core_op.second->infer_queue_size();
            CHECK_EXPECTED_AS_STATUS(queue_size_exp);
            const uint32_t queue_size = static_cast<uint32_t>(*queue_size_exp);

            for (const auto &stream_params : m_config_params.stream_params_by_name) {
                (stream_params.second.direction == HAILO_H2D_STREAM) ?
                    TRACE(AddStreamH2DTrace, core_op.first, name(), stream_params.first, queue_size, m_core_op_handle) :
                    TRACE(AddStreamD2HTrace, core_op.first, name(), stream_params.first, queue_size, m_core_op_handle);
            }
        }
    }

    return HAILO_SUCCESS;
}

bool VDeviceCoreOp::equal_batch(const std::map<std::string, hailo_network_parameters_t> &lhs, const std::map<std::string, hailo_network_parameters_t> &rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (const auto &lhs_pair : lhs) {
        if ((!contains(rhs, lhs_pair.first)) || (rhs.at(lhs_pair.first).batch_size != lhs_pair.second.batch_size)) {
            return false;
        }
    }

    return true;
}

} /* namespace hailort */
