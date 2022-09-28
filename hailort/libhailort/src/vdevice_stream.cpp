/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include <new>

#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/stream.hpp"
#include "hailo/hef.hpp"
#include "hailo/hailort_common.hpp"
#include "vdevice_stream.hpp"
#include "context_switch/multi_context/resource_manager.hpp"

namespace hailort
{

hailo_status VDeviceInputStream::deactivate_stream()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto deactivate_status = stream->deactivate_stream();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to deactivate input stream. (status: {} device: {})", deactivate_status, stream->get_dev_id());
            status = deactivate_status;
        }
    }
    m_is_stream_activated = false;
    return status;
}

/** Input stream **/
VDeviceInputStream::~VDeviceInputStream()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (m_is_stream_activated) {
        (void)deactivate_stream();
    }
}

hailo_status VDeviceInputStream::activate_stream(uint16_t dynamic_batch_size)
{
    for (auto &stream : m_streams) {
        auto status = stream->activate_stream(dynamic_batch_size);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to activate input stream. (device: {})", stream->get_dev_id());
            deactivate_stream();
            return status;
        }
    }
    m_is_stream_activated = true;
    return HAILO_SUCCESS;
}

Expected<size_t> VDeviceInputStream::sync_write_raw_buffer(const MemoryView &buffer)
{
    return sync_write_raw_buffer_impl(buffer, m_network_group_handle);
}

Expected<size_t> VDeviceInputStream::sync_write_raw_buffer_impl(const MemoryView &buffer, scheduler_ng_handle_t network_group_handle)
{
    size_t written_bytes = 0;
    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (network_group_scheduler) {
        auto status = network_group_scheduler->wait_for_write(network_group_handle, name());
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            LOGGER__INFO("Write to stream was aborted.");
            return make_unexpected(status);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);

        status = m_streams[m_next_transfer_stream_index]->write_buffer_only(buffer);
        if (HAILO_SUCCESS != status) {
            LOGGER__INFO("Write to stream has failed! status = {}", status);
            return make_unexpected(status);
        }

        status = network_group_scheduler->signal_write_finish(network_group_handle, name());
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return make_unexpected(status);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);

        written_bytes = buffer.size();
    } else {
        auto expected_written_bytes = m_streams[m_next_transfer_stream_index]->sync_write_raw_buffer(buffer);
        if (HAILO_SUCCESS != expected_written_bytes.status()) {
            LOGGER__INFO("Write to stream has failed! status = {}", expected_written_bytes.status());
            return make_unexpected(expected_written_bytes.status());
        }
        written_bytes = expected_written_bytes.value();
    }

    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams[0]->get_dynamic_batch_size())) {
        m_next_transfer_stream_index = static_cast<uint32_t>((m_next_transfer_stream_index + 1) % m_streams.size());
        m_acc_frames = 0;
    }
    return written_bytes;
}

hailo_status VDeviceInputStream::sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size)
{
    ASSERT(NULL != buffer);

    return sync_write_raw_buffer(MemoryView(static_cast<uint8_t*>(buffer) + offset, size)).status();
}

Expected<PendingBufferState> VDeviceInputStream::send_pending_buffer()
{
    assert(1 == m_streams.size());
    VdmaInputStream &vdma_input = static_cast<VdmaInputStream&>(*m_streams[m_next_transfer_stream_index].get());
    return vdma_input.send_pending_buffer();
}

Expected<size_t> VDeviceInputStream::get_buffer_frames_size() const
{
    size_t total_buffers_size = 0;
    for (auto &stream : m_streams) {
        auto stream_buffer_size = stream->get_buffer_frames_size();
        CHECK_EXPECTED(stream_buffer_size);
        total_buffers_size += stream_buffer_size.value();
    }
    
    return total_buffers_size;
}

Expected<size_t> VDeviceInputStream::get_pending_frames_count() const
{
    size_t total_pending_frames_count = 0;
    for (auto &stream : m_streams) {
        auto stream_pending_frames_count = stream->get_pending_frames_count();
        CHECK_EXPECTED(stream_pending_frames_count);
        total_pending_frames_count += stream_pending_frames_count.value();
    }
    
    return total_pending_frames_count;
}

// TODO - HRT-6830 - make create_input/output_stream_from_net_group as virutal function
Expected<std::shared_ptr<VDeviceInputStream>> VDeviceInputStream::create_input_stream_from_net_group(
    std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
    const LayerInfo &edge_layer, const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle,
    EventPtr &&network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler)
{
    hailo_status status = HAILO_UNINITIALIZED;

    std::vector<VdmaDevice*> devices;
    std::vector<std::unique_ptr<VdmaInputStream>> streams;

    for (auto &resources_manager : resources_managers) {
        auto vdma_channel_ptr_expected = resources_manager->get_boundary_vdma_channel_by_stream_name(stream_name);
        CHECK_EXPECTED(vdma_channel_ptr_expected);
        auto vdma_channel_ptr = vdma_channel_ptr_expected.release();

        auto batch_size_expected = resources_manager->get_network_batch_size(edge_layer.network_name);
        CHECK_EXPECTED(batch_size_expected);
        const auto batch_size = batch_size_expected.release();

        auto &device = resources_manager->get_device();
        devices.push_back(&device);

        auto local_stream = VdmaInputStream::create(device, vdma_channel_ptr, edge_layer, batch_size,
            network_group_activated_event);
        CHECK_EXPECTED(local_stream);
        streams.emplace_back(local_stream.release());
    }

    // Validate all streams share the same interface
    const auto stream_interface = streams[0]->get_interface();
    for (const auto &stream : streams) {
        CHECK_AS_EXPECTED(stream_interface == stream->get_interface(), HAILO_INTERNAL_FAILURE,
            "vDevice internal streams should have the same stream interface");
    }


    std::shared_ptr<VDeviceInputStream> local_vdevice_stream(new (std::nothrow) VDeviceInputStream(devices,
        std::move(streams), network_group_handle, std::move(network_group_activated_event), edge_layer, 
        network_group_scheduler, stream_interface, status));
    CHECK_AS_EXPECTED((nullptr != local_vdevice_stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

Expected<std::shared_ptr<VDeviceInputStream>> VDeviceInputStream::create(std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
    const LayerInfo &edge_layer, const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle,
    EventPtr network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler)
{
    assert(0 < resources_managers.size());

    auto input_stream = create_input_stream_from_net_group(resources_managers, edge_layer,
        stream_name, network_group_handle, std::move(network_group_activated_event), network_group_scheduler);
    CHECK_EXPECTED(input_stream);

    return input_stream.release();
}

hailo_status VDeviceInputStream::set_timeout(std::chrono::milliseconds timeout)
{
    for (auto &stream : m_streams) {
        auto status = stream->set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to input stream. (device: {})", stream->get_dev_id());
    }
    return HAILO_SUCCESS;
}

std::chrono::milliseconds VDeviceInputStream::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams[0]->get_timeout();
}

hailo_status VDeviceInputStream::flush()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto flush_status = stream->flush();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to flush input stream. (status: {} device: {})", status, stream->get_dev_id());
            status = flush_status;
        }
    }
    return status;
}

hailo_status VDeviceInputStream::abort()
{
    return abort_impl(m_network_group_handle);
}

hailo_status VDeviceInputStream::abort_impl(scheduler_ng_handle_t network_group_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto abort_status = stream->abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort input stream. (status: {} device: {})", status, stream->get_dev_id());
            status = abort_status;
        }
    }

    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (network_group_scheduler) {
        auto disable_status = network_group_scheduler->disable_stream(network_group_handle, name());
        if (HAILO_SUCCESS != disable_status) {
            LOGGER__ERROR("Failed to disable stream in the network group scheduler. (status: {})", disable_status);
            status = disable_status;
        }
    }

    return status;
}

hailo_status VDeviceInputStream::clear_abort()
{
    return clear_abort_impl(m_network_group_handle);
}

hailo_status VDeviceInputStream::clear_abort_impl(scheduler_ng_handle_t network_group_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto clear_abort_status = stream->clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort input stream. (status: {} device: {})", clear_abort_status, stream->get_dev_id());
            status = clear_abort_status;
        }
    }

    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (network_group_scheduler) {
        auto enable_status = network_group_scheduler->enable_stream(network_group_handle, name());
        if (HAILO_SUCCESS != enable_status) {
            LOGGER__ERROR("Failed to enable stream in the network group scheduler. (status: {})", enable_status);
            status = enable_status;
        }
    }

    return status;
}

bool VDeviceInputStream::is_scheduled()
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (!network_group_scheduler) {
        return false;
    }
    return (HAILO_SCHEDULING_ALGORITHM_NONE != network_group_scheduler->algorithm());
}

/** Output stream **/
hailo_status VDeviceOutputStream::deactivate_stream()
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto deactivate_status = stream->deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate output stream. (status: {} device: {})", status, stream->get_dev_id());
            status = deactivate_status;
        }
    }
    m_is_stream_activated = false;
    return status;
}

VDeviceOutputStream::~VDeviceOutputStream()
{
    // We want to stop the vdma channel before closing the stream in the firmware
    // because sending data to a closed stream may terminate the dma engine
    if (m_is_stream_activated) {
        (void)deactivate_stream();
    }
}

hailo_status VDeviceOutputStream::activate_stream(uint16_t dynamic_batch_size)
{
    for (auto &stream : m_streams) {
        auto status = stream->activate_stream(dynamic_batch_size);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to activate output stream. (device: {})", stream->get_dev_id());
            deactivate_stream();
            return status;
        }
    }
    m_is_stream_activated = true;
    return HAILO_SUCCESS;
}

hailo_status VDeviceOutputStream::read_all(MemoryView &/*buffer*/)
{
    LOGGER__ERROR("read_all should not be called in vdevice flow");
    return HAILO_INTERNAL_FAILURE;
}

Expected<size_t> VDeviceOutputStream::sync_read_raw_buffer(MemoryView &/*buffer*/)
{
    LOGGER__ERROR("sync_read_raw_buffer should not be called in vdevice flow");
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

hailo_status VDeviceOutputStream::read(MemoryView buffer)
{
    return read_impl(buffer, m_network_group_handle);
}

hailo_status VDeviceOutputStream::read_impl(MemoryView buffer, scheduler_ng_handle_t network_group_handle)
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (network_group_scheduler) {
        auto status = network_group_scheduler->wait_for_read(network_group_handle, name());
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            LOGGER__INFO("Read from stream was aborted.");
            return status;
        }
        CHECK_SUCCESS(status);
    }

    auto status = m_streams[m_next_transfer_stream_index]->read(buffer);
    if (HAILO_SUCCESS != status) {
        LOGGER__INFO("Read from stream has failed! status = {}", status);
        return status;
    }

    // Update m_next_transfer_stream_index only if 'batch' frames has been transferred
    if (0 == (++m_acc_frames % m_streams[0]->get_dynamic_batch_size())) {
        m_next_transfer_stream_index = static_cast<uint32_t>((m_next_transfer_stream_index + 1) % m_streams.size());
        m_acc_frames = 0;
    }

    if (network_group_scheduler) {
        status = network_group_scheduler->signal_read_finish(network_group_handle, name());
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

// TODO - HRT-6830 - make create_input/output_stream_from_net_group as virutal function
Expected<std::unique_ptr<VDeviceOutputStream>> VDeviceOutputStream::create_output_stream_from_net_group(
    std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
    const LayerInfo &edge_layer, const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle,
    EventPtr &&network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler)
{
    hailo_status status = HAILO_UNINITIALIZED;

    std::vector<VdmaDevice*> devices;
    std::vector<std::unique_ptr<VdmaOutputStream>> streams;

    for (auto &resources_manager : resources_managers) {
        auto vdma_channel_ptr_expected = resources_manager->get_boundary_vdma_channel_by_stream_name(stream_name);
        CHECK_EXPECTED(vdma_channel_ptr_expected);
        auto vdma_channel_ptr = vdma_channel_ptr_expected.release();

        auto batch_size_expected = resources_manager->get_network_batch_size(edge_layer.network_name);
        CHECK_EXPECTED(batch_size_expected);
        const auto batch_size = batch_size_expected.release();

        auto &device = resources_manager->get_device();
        devices.push_back(&device);

        auto local_stream = VdmaOutputStream::create(device, vdma_channel_ptr, edge_layer, batch_size,
            network_group_activated_event);
        CHECK_EXPECTED(local_stream);
        streams.emplace_back(local_stream.release());
    }

    // Validate all streams share the same interface
    const auto stream_interface = streams[0]->get_interface();
    for (const auto &stream : streams) {
        CHECK_AS_EXPECTED(stream_interface == stream->get_interface(), HAILO_INTERNAL_FAILURE,
            "vDevice internal streams should have the same stream interface");
    }

    std::unique_ptr<VDeviceOutputStream> local_vdevice_stream(new (std::nothrow) VDeviceOutputStream(devices,
        std::move(streams), network_group_handle, edge_layer, std::move(network_group_activated_event),
        network_group_scheduler, stream_interface, status));
    CHECK_AS_EXPECTED((nullptr != local_vdevice_stream), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_vdevice_stream;
}

Expected<std::unique_ptr<VDeviceOutputStream>> VDeviceOutputStream::create(std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
    const LayerInfo &edge_layer, const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle, EventPtr network_group_activated_event,
    NetworkGroupSchedulerWeakPtr network_group_scheduler)
{
    assert(0 < resources_managers.size());

    auto output_stream = create_output_stream_from_net_group(resources_managers, edge_layer,
        stream_name, network_group_handle, std::move(network_group_activated_event), network_group_scheduler);
    CHECK_EXPECTED(output_stream);

    return output_stream.release();
}

hailo_status VDeviceOutputStream::set_timeout(std::chrono::milliseconds timeout)
{
    for (auto &stream : m_streams) {
        auto status = stream->set_timeout(timeout);
        CHECK_SUCCESS(status, "Failed to set timeout to output stream. (device: {})", stream->get_dev_id());
    }
    return HAILO_SUCCESS;
}

std::chrono::milliseconds VDeviceOutputStream::get_timeout() const
{
    // All timeout values of m_streams should be the same
    return m_streams[0]->get_timeout();
}

hailo_status VDeviceOutputStream::abort()
{
    return abort_impl(m_network_group_handle);
}

hailo_status VDeviceOutputStream::abort_impl(scheduler_ng_handle_t network_group_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto abort_status = stream->abort();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to abort output stream. (status: {} device: {})", status, stream->get_dev_id());
            status = abort_status;
        }
    }

    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (network_group_scheduler) {
        auto disable_status = network_group_scheduler->disable_stream(network_group_handle, name());
        if (HAILO_SUCCESS != disable_status) {
            LOGGER__ERROR("Failed to disable stream in the network group scheduler. (status: {})", disable_status);
            status = disable_status;
        }
    }

    return status;
}

hailo_status VDeviceOutputStream::clear_abort()
{
    return clear_abort_impl(m_network_group_handle);
}

hailo_status VDeviceOutputStream::clear_abort_impl(scheduler_ng_handle_t network_group_handle)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &stream : m_streams) {
        auto clear_abort_status = stream->clear_abort();
        if ((HAILO_SUCCESS != clear_abort_status) && (HAILO_STREAM_NOT_ACTIVATED != clear_abort_status)) {
            LOGGER__ERROR("Failed to clear abort output stream. (status: {} device: {})", clear_abort_status, stream->get_dev_id());
            status = clear_abort_status;
        }
    }

    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (network_group_scheduler) {
        auto enable_status = network_group_scheduler->enable_stream(network_group_handle, name());
        if (HAILO_SUCCESS != enable_status) {
            LOGGER__ERROR("Failed to enable stream in the network group scheduler. (status: {})", enable_status);
            status = enable_status;
        }
    }

    return status;
}

bool VDeviceOutputStream::is_scheduled()
{
    auto network_group_scheduler = m_network_group_scheduler.lock();
    if (!network_group_scheduler) {
        return false;
    }
    return (HAILO_SCHEDULING_ALGORITHM_NONE != network_group_scheduler->algorithm());
}

Expected<size_t> VDeviceOutputStream::get_buffer_frames_size() const
{
    size_t total_buffers_size = 0;
    for (auto &stream : m_streams) {
        auto stream_buffer_size = stream->get_buffer_frames_size();
        if (HAILO_NOT_AVAILABLE == stream_buffer_size.status()) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        CHECK_EXPECTED(stream_buffer_size);
        total_buffers_size += stream_buffer_size.value();
    }
    
    return total_buffers_size;
}

Expected<size_t> VDeviceOutputStream::get_pending_frames_count() const
{
    size_t total_pending_frames_count = 0;
    for (auto &stream : m_streams) {
        auto stream_pending_frames_count = stream->get_pending_frames_count();
        if (HAILO_NOT_AVAILABLE == stream_pending_frames_count.status()) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        CHECK_EXPECTED(stream_pending_frames_count);
        total_pending_frames_count += stream_pending_frames_count.value();
    }
    
    return total_pending_frames_count;
}

} /* namespace hailort */
