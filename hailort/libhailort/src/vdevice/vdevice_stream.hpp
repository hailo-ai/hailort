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

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "vdevice/vdevice_internal.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/vdma_stream.hpp"
#include "stream_common/stream_internal.hpp"


namespace hailort
{

class InputVDeviceBaseStream : public InputStreamBase {

public:
    static Expected<std::unique_ptr<InputVDeviceBaseStream>> create(std::vector<std::reference_wrapper<VdmaInputStream>> &&low_level_streams,
        const LayerInfo &edge_layer, const scheduler_core_op_handle_t &core_op_handle,
        EventPtr core_op_activated_event, CoreOpsSchedulerWeakPtr core_ops_scheduler);

    virtual ~InputVDeviceBaseStream();

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
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

    virtual hailo_status register_interrupt_callback(const vdma::ProcessingCompleteCallback &callback) override
    {
        for (auto &stream : m_streams) {
            auto status = stream.get().register_interrupt_callback(callback);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

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
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        hailo_status &status) :
            InputStreamBase(layer_info, streams[0].get().get_interface(), std::move(core_op_activated_event), status),
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
    virtual ~OutputVDeviceBaseStream();

    static Expected<std::unique_ptr<OutputVDeviceBaseStream>> create(std::vector<std::reference_wrapper<VdmaOutputStream>> &&low_level_streams,
        const LayerInfo &edge_layer, const scheduler_core_op_handle_t &core_op_handle,
        EventPtr core_op_activated_event, CoreOpsSchedulerWeakPtr core_ops_scheduler);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;
    virtual hailo_status abort() override = 0;
    virtual hailo_status clear_abort() override = 0;
    virtual bool is_scheduled() override = 0;

    virtual hailo_status register_interrupt_callback(const vdma::ProcessingCompleteCallback &callback) override
    {
        for (auto &stream : m_streams) {
            auto status = stream.get().register_interrupt_callback(callback);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

protected:
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer) override;

    explicit OutputVDeviceBaseStream(
        std::vector<std::reference_wrapper<VdmaOutputStream>> &&streams,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        hailo_status &status) :
            OutputStreamBase(layer_info, std::move(core_op_activated_event), status),
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
