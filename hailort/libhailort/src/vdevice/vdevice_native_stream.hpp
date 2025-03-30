/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_native_stream.hpp
 * @brief Internal stream implementation for native streams
 *
 **/

#ifndef HAILO_VDEVICE_NATIVE_STREAM_HPP_
#define HAILO_VDEVICE_NATIVE_STREAM_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "stream_common/stream_internal.hpp"
#include "vdevice/callback_reorder_queue.hpp"
#include "vdevice/vdevice_core_op.hpp"


namespace hailort
{


class VDeviceNativeInputStream : public InputStreamBase {
public:
    static Expected<std::unique_ptr<VDeviceNativeInputStream>> create(
        std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
        EventPtr core_op_activated_event,
        const LayerInfo &layer_info,
        uint16_t batch_size,
        vdevice_core_op_handle_t core_op_handle);

    VDeviceNativeInputStream(
        std::map<device_id_t, std::reference_wrapper<InputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        uint16_t batch_size,
        vdevice_core_op_handle_t core_op_handle,
        std::unique_ptr<CallbackReorderQueue> &&callback_reorder_queue,
        hailo_status &status) :
            InputStreamBase(layer_info, std::move(core_op_activated_event), status),
            m_streams(std::move(streams)),
            m_next_transfer_stream(m_streams.begin()->first),
            m_acc_frames(0),
            m_batch_size(batch_size),
            m_callback_reorder_queue(std::move(callback_reorder_queue)),
            m_core_op_handle(core_op_handle)
    {}

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override;
    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override;
    virtual bool is_scheduled() override { return false; };

    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;

    virtual hailo_status flush() override;

    virtual hailo_status write_impl(const MemoryView &buffer) override;
    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(TransferRequest &&transfer_request) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;

protected:

    InputStreamBase &next_stream();
    void advance_stream();

    std::map<device_id_t, std::reference_wrapper<InputStreamBase>> m_streams;
    device_id_t m_next_transfer_stream;
    uint32_t m_acc_frames;
    const uint16_t m_batch_size;
    std::unique_ptr<CallbackReorderQueue> m_callback_reorder_queue;
    vdevice_core_op_handle_t m_core_op_handle;

};

class VDeviceNativeOutputStream : public OutputStreamBase {
public:
    static Expected<std::unique_ptr<VDeviceNativeOutputStream>> create(
        std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
        EventPtr core_op_activated_event, const LayerInfo &layer_info, uint16_t batch_size,
        vdevice_core_op_handle_t core_op_handle);

    VDeviceNativeOutputStream(
        std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        uint16_t batch_size,
        vdevice_core_op_handle_t core_op_handle,
        std::unique_ptr<CallbackReorderQueue> &&callback_reorder_queue,
        hailo_status &status) :
            OutputStreamBase(layer_info, std::move(core_op_activated_event), status),
            m_streams(std::move(streams)),
            m_next_transfer_stream(m_streams.begin()->first),
            m_acc_frames(0),
            m_batch_size(batch_size),
            m_core_op_handle(core_op_handle),
            m_callback_reorder_queue(std::move(callback_reorder_queue))
    {}

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override;
    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override;
    virtual bool is_scheduled() override { return false; };
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;

    virtual hailo_status read_impl(MemoryView buffer) override;

    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status read_async(TransferRequest &&transfer_request) override;
    virtual hailo_status read_unaligned_address_async(const MemoryView &buffer,
        const TransferDoneCallback &user_callback) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;

private:
    OutputStreamBase &next_stream();
    void advance_stream();

    std::map<device_id_t, std::reference_wrapper<OutputStreamBase>> m_streams;
    device_id_t m_next_transfer_stream;
    uint32_t m_acc_frames;
    const uint16_t m_batch_size;
    vdevice_core_op_handle_t m_core_op_handle;
    std::unique_ptr<CallbackReorderQueue> m_callback_reorder_queue;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_NATIVE_STREAM_HPP_ */
