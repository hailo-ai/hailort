/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream.hpp
 * @brief Internal stream implementation for VDevice
 *
 * InputStream                             (External "interface")
 * |-- InputStreamBase                     (Base class)
 *     |-- InputVDeviceBaseStream          (Base class for vdevice streams)
 *     |   |-- InputVDeviceNativeStream
 *     |   |-- ScheduledInputStream
 *
 * OutputStream                             (External "interface")
 * |-- OutputStreamBase                     (Base class)
 *     |-- OutputVDeviceBaseStream          (Base class for vdevice streams)
 *     |   |-- OutputVDeviceNativeStream
 *     |   |-- ScheduledOutputStream
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

class InputVDeviceBaseStream : public InputStreamBase {

public:
    static Expected<std::unique_ptr<InputVDeviceBaseStream>> create(std::vector<std::reference_wrapper<VdmaInputStream>> &&low_level_streams,
        const LayerInfo &edge_layer, const scheduler_ng_handle_t &network_group_handle,
        EventPtr network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler);

    InputVDeviceBaseStream(InputVDeviceBaseStream &&other) :
        InputStreamBase(std::move(other)),
        m_streams(std::move(other.m_streams)),
        m_is_stream_activated(std::exchange(other.m_is_stream_activated, false)),
        m_next_transfer_stream_index(other.m_next_transfer_stream_index),
        m_acc_frames(other.m_acc_frames)
    {}

    virtual ~InputVDeviceBaseStream();

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;

    virtual hailo_status send_pending_buffer(size_t device_index = 0) override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;
    virtual bool is_scheduled() override = 0;
    virtual hailo_status abort() override = 0;
    virtual hailo_status clear_abort() override = 0;

    virtual void notify_all()
    {
        // Overriden in scheduled_stream
        return;
    }

protected:
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override
    {
        return sync_write_raw_buffer(buffer, []() { return false; });
    }
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer, const std::function<bool()> &should_cancel) = 0;

    explicit InputVDeviceBaseStream(
        std::vector<std::reference_wrapper<VdmaInputStream>> &&streams,
        EventPtr &&network_group_activated_event,
        const LayerInfo &layer_info,
        hailo_status &status) :
            InputStreamBase(layer_info, streams[0].get().get_interface(), std::move(network_group_activated_event), status),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream_index(0),
            m_acc_frames(0)
    {}

    std::vector<std::reference_wrapper<VdmaInputStream>> m_streams;
    bool m_is_stream_activated;
    uint32_t m_next_transfer_stream_index;
    uint32_t m_acc_frames;

private:
    friend class VDeviceInputStreamMultiplexerWrapper;

    virtual hailo_status flush() override;
};

class OutputVDeviceBaseStream : public OutputStreamBase {
public:
    OutputVDeviceBaseStream(OutputVDeviceBaseStream &&other) :
        OutputStreamBase(std::move(other)),
        m_streams(std::move(other.m_streams)),
        m_is_stream_activated(std::exchange(other.m_is_stream_activated, false)),
        m_next_transfer_stream_index(other.m_next_transfer_stream_index),
        m_acc_frames(other.m_acc_frames)
    {}

    virtual ~OutputVDeviceBaseStream();

    static Expected<std::unique_ptr<OutputVDeviceBaseStream>> create(std::vector<std::reference_wrapper<VdmaOutputStream>> &&low_level_streams,
        const LayerInfo &edge_layer, const scheduler_ng_handle_t &network_group_handle,
        EventPtr network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;
    virtual hailo_status abort() override = 0;
    virtual hailo_status clear_abort() override = 0;
    virtual bool is_scheduled() override = 0;

    virtual hailo_status register_for_d2h_interrupts(const std::function<void(uint32_t)> &callback) override
    {
        for (auto &stream : m_streams) {
            auto status = stream.get().register_for_d2h_interrupts(callback);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

protected:
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer) override;

    explicit OutputVDeviceBaseStream(
        std::vector<std::reference_wrapper<VdmaOutputStream>> &&streams,
        const LayerInfo &layer_info,
        EventPtr &&network_group_activated_event,
        hailo_status &status) :
            OutputStreamBase(layer_info, std::move(network_group_activated_event), status),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream_index(0),
            m_acc_frames(0)
    {}

    virtual hailo_status read_all(MemoryView &buffer) override;

    std::vector<std::reference_wrapper<VdmaOutputStream>> m_streams;
    bool m_is_stream_activated;
    uint32_t m_next_transfer_stream_index;
    uint32_t m_acc_frames;

private:
    friend class VDeviceOutputStreamMultiplexerWrapper;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_STREAM_HPP_ */
