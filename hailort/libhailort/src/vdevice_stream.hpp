/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream.hpp
 * @brief Internal stream implementation for VDevice - holds VdmaStream per physical device
 *
 **/

#ifndef HAILO_VDEVICE_STREAM_HPP_
#define HAILO_VDEVICE_STREAM_HPP_

#include "stream_internal.hpp"
#include "hailo/hailort.h"
#include "vdevice_internal.hpp"
#include "vdma_device.hpp"
#include "vdma_stream.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

class VDeviceInputStream : public InputStreamBase {
public:
    VDeviceInputStream(VDeviceInputStream &&other) :
        InputStreamBase(std::move(other)),
        m_network_group_handle(std::move(other.m_network_group_handle)),
        m_network_group_scheduler(std::move(other.m_network_group_scheduler)),
        m_devices(std::move(other.m_devices)),
        m_streams(std::move(other.m_streams)),
        m_is_stream_activated(std::exchange(other.m_is_stream_activated, false)),
        m_next_transfer_stream_index(other.m_next_transfer_stream_index),
        m_acc_frames(other.m_acc_frames),
        m_stream_interface(other.m_stream_interface)
    {}

    virtual ~VDeviceInputStream();

    static Expected<std::shared_ptr<VDeviceInputStream>> create(std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
        const LayerInfo &edge_layer, const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle, EventPtr network_group_activated_event,
        NetworkGroupSchedulerWeakPtr network_group_scheduler);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override { return m_stream_interface; }
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;

    virtual Expected<PendingBufferState> send_pending_buffer() override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;
    virtual hailo_status reset_offset_of_pending_frames() override;

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;

private:
    friend class VDeviceInputStreamWrapper;

    explicit VDeviceInputStream(
        std::vector<VdmaDevice*> devices,
        std::vector<std::unique_ptr<VdmaInputStream>> &&streams,
        const scheduler_ng_handle_t &network_group_handle,
        EventPtr &&network_group_activated_event,
        const LayerInfo &layer_info,
        NetworkGroupSchedulerWeakPtr network_group_scheduler,
        hailo_stream_interface_t stream_interface,
        hailo_status &status) :
            InputStreamBase(layer_info, stream_interface, std::move(network_group_activated_event), status),
            m_network_group_handle(network_group_handle),
            m_network_group_scheduler(network_group_scheduler),
            m_devices(devices),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream_index(0),
            m_acc_frames(0),
            m_stream_interface(stream_interface)
    {}

    Expected<size_t> sync_write_raw_buffer_impl(const MemoryView &buffer, scheduler_ng_handle_t network_group_handle);
    hailo_status abort_impl(scheduler_ng_handle_t network_group_handle);
    hailo_status clear_abort_impl(scheduler_ng_handle_t network_group_handle);
    virtual hailo_status flush() override;

    static Expected<std::shared_ptr<VDeviceInputStream>> create_input_stream_from_net_group(
        std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
        const LayerInfo &edge_layer, const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle,
        EventPtr &&network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler);

    scheduler_ng_handle_t m_network_group_handle;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;
    std::vector<VdmaDevice*> m_devices;
    std::vector<std::unique_ptr<VdmaInputStream>> m_streams;
    bool m_is_stream_activated;
    uint32_t m_next_transfer_stream_index;
    uint32_t m_acc_frames;
    hailo_stream_interface_t m_stream_interface;
};

class VDeviceOutputStream : public OutputStreamBase {
public:
    VDeviceOutputStream(VDeviceOutputStream &&other) :
        OutputStreamBase(std::move(other)),
        m_network_group_handle(std::move(other.m_network_group_handle)),
        m_network_group_scheduler(std::move(other.m_network_group_scheduler)),
        m_devices(std::move(other.m_devices)),
        m_streams(std::move(other.m_streams)),
        m_is_stream_activated(std::exchange(other.m_is_stream_activated, false)),
        m_next_transfer_stream_index(other.m_next_transfer_stream_index),
        m_acc_frames(other.m_acc_frames),
        m_stream_interface(other.m_stream_interface)
    {}

    virtual ~VDeviceOutputStream();

    static Expected<std::unique_ptr<VDeviceOutputStream>> create(std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
        const LayerInfo &edge_layer, const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle,
        EventPtr network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override { return m_stream_interface; }
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;

    virtual hailo_status register_for_d2h_interrupts(const std::function<void(uint32_t)> &callback) override
    {
        for (auto &stream : m_streams) {
            auto status = stream->register_for_d2h_interrupts(callback);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

protected:
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer) override;

private:
    friend class VDeviceOutputStreamWrapper;

    explicit VDeviceOutputStream(
        std::vector<VdmaDevice*> devices,
        std::vector<std::unique_ptr<VdmaOutputStream>> &&streams,
        const scheduler_ng_handle_t &network_group_handle,
        const LayerInfo &layer_info,
        EventPtr &&network_group_activated_event,
        NetworkGroupSchedulerWeakPtr network_group_scheduler,
        hailo_stream_interface_t stream_interface,
        hailo_status &status) :
            OutputStreamBase(layer_info, std::move(network_group_activated_event), status),
            m_network_group_handle(network_group_handle),
            m_network_group_scheduler(network_group_scheduler),
            m_devices(devices),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream_index(0),
            m_acc_frames(0),
            m_stream_interface(stream_interface)
    {}

    hailo_status abort_impl(scheduler_ng_handle_t network_group_handle);
    hailo_status clear_abort_impl(scheduler_ng_handle_t network_group_handle);
    virtual hailo_status read_all(MemoryView &buffer) override;
    virtual hailo_status read(MemoryView buffer) override;
    hailo_status read_impl(MemoryView buffer, scheduler_ng_handle_t network_group_handle);
    
    static Expected<std::unique_ptr<VDeviceOutputStream>> create_output_stream_from_net_group(
        std::vector<std::shared_ptr<ResourcesManager>> &resources_managers, const LayerInfo &edge_layer,
        const std::string &stream_name, const scheduler_ng_handle_t &network_group_handle, EventPtr &&network_group_activated_event,
        NetworkGroupSchedulerWeakPtr network_group_scheduler);

    scheduler_ng_handle_t m_network_group_handle;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;
    std::vector<VdmaDevice*> m_devices;
    std::vector<std::unique_ptr<VdmaOutputStream>> m_streams;
    bool m_is_stream_activated;
    uint32_t m_next_transfer_stream_index;
    uint32_t m_acc_frames;
    hailo_stream_interface_t m_stream_interface;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_STREAM_HPP_ */
