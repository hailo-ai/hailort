/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream.hpp
 * @brief Internal stream implementation for VDevice
 *
 * InputStream                                      (External "interface")
 * |-- InputStreamBase                              (Base class)
 *     |-- VDeviceInputStreamBase                   (Base class for vdevice streams)
 *     |   |-- VDeviceNativeInputStreamBase
 *     |   |    |-- VDeviceNativeInputStream        (Sync api)
 *     |   |    |-- VDeviceNativeAsyncInputStream   (Async api)
 *     |   |-- ScheduledInputStreamBase
 *     |   |    |-- ScheduledInputStream            (Sync api)
 *     |   |    |-- ScheduledAsyncInputStream       (Async api)
 *
 * OutputStream                                     (External "interface")
 * |-- OutputStreamBase                             (Base class)
 *     |-- VDeviceOutputStreamBase                  (Base class for vdevice streams)
 *     |   |-- VDeviceNativeOutputStreamBase
 *     |   |    |-- VDeviceNativeOutputStream       (Sync api)
 *     |   |    |-- VDeviceNativeAsyncOutputStream  (Async api)
 *     |   |-- ScheduledOutputStreamBase
 *     |   |    |-- ScheduledOutputStream           (Sync api)
 *     |   |    |-- ScheduledAsyncOutputStream      (Async api)
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

class VDeviceInputStreamBase : public InputStreamBase {

public:
    static Expected<std::unique_ptr<VDeviceInputStreamBase>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&low_level_streams,
        const hailo_stream_parameters_t &stream_params, const LayerInfo &edge_layer,
        const scheduler_core_op_handle_t &core_op_handle, EventPtr core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler);

    virtual ~VDeviceInputStreamBase();

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;

    virtual hailo_status send_pending_buffer(const device_id_t &device_id) override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;
    virtual bool is_scheduled() override = 0;
    virtual hailo_status abort() override = 0;
    virtual hailo_status clear_abort() override = 0;
    virtual hailo_status flush() override;

    virtual void notify_all()
    {
        // Overriden in scheduled_stream
        return;
    }

protected:
    virtual hailo_status write_impl(const MemoryView &buffer) final override;
    virtual hailo_status write_impl(const MemoryView &buffer, const std::function<bool()> &should_cancel) = 0;

    VDeviceInputStreamBase(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        hailo_status &status) :
            InputStreamBase(layer_info, streams.begin()->second.get().get_interface(), std::move(core_op_activated_event), status),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream(m_streams.begin()->first),
            m_acc_frames(0)
    {}

    std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> m_streams;
    bool m_is_stream_activated;
    device_id_t m_next_transfer_stream;
    uint32_t m_acc_frames;

private:
    friend class VDeviceInputStreamMultiplexerWrapper;
};

class VDeviceOutputStreamBase : public OutputStreamBase {
public:
    virtual ~VDeviceOutputStreamBase();

    static Expected<std::unique_ptr<VDeviceOutputStreamBase>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&low_level_streams,
        const hailo_stream_parameters_t &stream_params, const LayerInfo &edge_layer,
        const scheduler_core_op_handle_t &core_op_handle, EventPtr core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override; // Returns the accumulated pending frames
    virtual hailo_status abort() override = 0;
    virtual hailo_status clear_abort() override = 0;
    virtual bool is_scheduled() override = 0;

protected:
    VDeviceOutputStreamBase(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        hailo_status &status) :
            OutputStreamBase(layer_info, streams.begin()->second.get().get_interface(),
                std::move(core_op_activated_event), status),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream(m_streams.begin()->first),
            m_acc_frames(0)
    {}

    virtual hailo_status read_impl(MemoryView &buffer) override final;

    std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> m_streams;
    bool m_is_stream_activated;
    device_id_t m_next_transfer_stream;
    uint32_t m_acc_frames;

private:
    friend class VDeviceOutputStreamMultiplexerWrapper;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_STREAM_HPP_ */
