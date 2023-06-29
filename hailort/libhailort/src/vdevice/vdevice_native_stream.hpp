/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "vdevice_stream.hpp"
#include "vdevice/callback_reorder_queue.hpp"


namespace hailort
{


class VDeviceNativeInputStreamBase : public VDeviceInputStreamBase {
public:
    static Expected<std::unique_ptr<VDeviceNativeInputStreamBase>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info);

    VDeviceNativeInputStreamBase(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        hailo_status &status) :
            VDeviceInputStreamBase(std::move(streams), std::move(core_op_activated_event), layer_info, status)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return false; };
};

class VDeviceNativeInputStream : public VDeviceNativeInputStreamBase {
public:
    static Expected<std::unique_ptr<VDeviceNativeInputStream>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info);

    using VDeviceNativeInputStreamBase::VDeviceNativeInputStreamBase;

protected:
    virtual hailo_status write_impl(const MemoryView &buffer, const std::function<bool()> &should_cancel) override;\
};

class VDeviceNativeAsyncInputStream : public VDeviceNativeInputStreamBase {
public:
    static Expected<std::unique_ptr<VDeviceNativeAsyncInputStream>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info);

    VDeviceNativeAsyncInputStream(
        std::map<device_id_t, std::reference_wrapper<VdmaInputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        size_t max_queue_size,
        hailo_status &status) :
            VDeviceNativeInputStreamBase(std::move(streams), std::move(core_op_activated_event), layer_info, status),
            m_callback_reorder_queue(max_queue_size), // TODO HRT-1058 - use reorder queue only when needed
            m_max_queue_size(max_queue_size)
    {}

    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(TransferRequest &&transfer_request) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;

protected:
    virtual hailo_status write_impl(const MemoryView &buffer, const std::function<bool()> &should_cancel) override;

private:
    CallbackReorderQueue m_callback_reorder_queue;
    const size_t m_max_queue_size;
};

class VDeviceNativeOutputStreamBase : public VDeviceOutputStreamBase {
public:
    VDeviceNativeOutputStreamBase(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        hailo_status &status) :
            VDeviceOutputStreamBase(std::move(streams), layer_info, std::move(core_op_activated_event), status)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return false; };
};

class VDeviceNativeOutputStream : public VDeviceNativeOutputStreamBase {
public:
    static Expected<std::unique_ptr<VDeviceNativeOutputStream>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event, const LayerInfo &layer_info);

    using VDeviceNativeOutputStreamBase::VDeviceNativeOutputStreamBase;
    virtual hailo_status read(MemoryView buffer) override;
};

class VDeviceNativeAsyncOutputStream : public VDeviceNativeOutputStreamBase {
public:
    static Expected<std::unique_ptr<VDeviceNativeAsyncOutputStream>> create(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event, const LayerInfo &layer_info);

    VDeviceNativeAsyncOutputStream(
        std::map<device_id_t, std::reference_wrapper<VdmaOutputStreamBase>> &&streams,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        size_t max_queue_size,
        hailo_status &status) :
            VDeviceNativeOutputStreamBase(std::move(streams), std::move(core_op_activated_event), layer_info, status),
            m_callback_reorder_queue(max_queue_size), // TODO HRT-1058 - use reorder queue only when needed
            m_max_queue_size(max_queue_size)
    {}

    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status read_async(TransferRequest &&transfer_request) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;
    virtual hailo_status read(MemoryView buffer) override;

private:
    CallbackReorderQueue m_callback_reorder_queue;
    const size_t m_max_queue_size;
 };

} /* namespace hailort */

#endif /* HAILO_VDEVICE_NATIVE_STREAM_HPP_ */
