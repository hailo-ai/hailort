/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file stream_internal.hpp
 * @brief Class declaration for InputStreamBase/OutputStreamBase that implement the basic InputStream/OutputStream
 *        "interface" (not technically an interface, but good enough). All internal input/output streams
 *        should inherit from the InputStreamBase/OutputStreamBase classes.
 *        Hence, the hierarchy is as follows:
 *
 * InputStream                      (External "interface")
 * |-- InputStreamBase              (Base class)
 *     |-- AsyncInputStreamBase
 *          |-- VdmaInputStream
 *          |-- ScheduledInputStream
 *     |-- EthernetInputStream
 *     |-- MipiInputStream
 *     |-- RemoteProcessInputStream (used for pyhailort to support fork)
 *     |-- VDeviceNativeInputStream
 *
 * OutputStream                      (External "interface")
 * |-- OutputStreamBase              (Base class)
 *     |-- AsyncOutputStreamBase
 *          |-- VdmaOutputStream
 *          |-- NmsOutputStream (wraps other OutputStreamBase, accumulate bbox/burst reads into frame reads).
 *          |-- ScheduledOutputStream
 *     |-- EthernetOutputStream
 *     |-- RemoteProcessOutputStream (used for pyhailort to support fork)
 *     |-- VDeviceNativeOutputStream
 **/

#ifndef _STREAM_INTERNAL_HPP_
#define _STREAM_INTERNAL_HPP_

#include "hailo/stream.hpp"
#include "hailo/event.hpp"
#include "hailo/hailort_common.hpp"

#include "vdma/channel/transfer_common.hpp"
#include "device_common/control_protocol.hpp"
#include "hef/layer_info.hpp"


namespace hailort
{

typedef struct hailo_mux_info_t{
    hailo_stream_info_t info;
    uint32_t row_size;
    uint32_t row_counter;
    uint32_t rows_gcd;
    uint32_t offset;
    uint32_t current_offset; // Current demuxing offset
    uint32_t successors_count;
    struct hailo_mux_info_t *successors[HailoRTCommon::MAX_MUX_PREDECESSORS];
    void* buffer;
} hailo_mux_info_t;


enum class StreamBufferMode {
    // The buffer mode is not determined yet.
    // It will be set automatically based on the functions call (For example, calling write_async on input stream force
    // usage of NOT_OWNING mode) or manually by calling set_stream_mode()
    NOT_SET,

    // The buffer is owned by the stream. On each write/read call we copy the buffer into/from the stream buffer.
    OWNING,

    // The buffer is owned by the user. On each write_async/read_async call, we launch the transfer directly on the
    // user buffer.
    NOT_OWNING
};

class InputStreamBase : public InputStream
{
public:
    virtual ~InputStreamBase() = default;

    // Manually set the buffer mode, fails if the mode was already set (and different from buffer_mode)
    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) = 0;

    const LayerInfo& get_layer_info()
    {
        return m_layer_info;
    };

    const std::vector<hailo_quant_info_t> &get_quant_infos() const
    {
        return m_quant_infos;
    }

    virtual hailo_status write(const MemoryView &buffer) override final;
    virtual hailo_status write(const void *buffer, size_t size) override final;

    virtual hailo_status write_impl(const MemoryView &buffer) = 0;

    virtual hailo_status write_async(const MemoryView &buffer, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status write_async(const void *buffer, size_t size, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status write_async(int dmabuf_fd, size_t size, const TransferDoneCallback &user_callback) override final;

    virtual hailo_status bind_buffer(TransferRequest &&transfer_request);

    virtual hailo_status write_async(TransferRequest &&transfer_request);

    virtual hailo_status abort() override final;
    virtual hailo_status abort_impl() = 0;

    virtual hailo_status clear_abort() override final;
    virtual hailo_status clear_abort_impl() = 0;

    virtual EventPtr &get_core_op_activated_event() override;
    virtual bool is_scheduled() override;

    virtual hailo_status activate_stream() = 0;
    virtual hailo_status deactivate_stream() = 0;

    virtual void set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle);

    virtual hailo_status cancel_pending_transfers();

protected:
    explicit InputStreamBase(const LayerInfo &layer_info, EventPtr core_op_activated_event, hailo_status &status) :
        m_layer_info(layer_info),
        m_core_op_activated_event(std::move(core_op_activated_event))
    {
        const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
        assert(1 == stream_infos.size());
        m_stream_info = stream_infos[0];
        m_quant_infos = layer_info.quant_infos;
        status = HAILO_SUCCESS;
    }

    LayerInfo m_layer_info;

private:

    EventPtr m_core_op_activated_event;
};

class OutputStreamBase : public OutputStream
{
public:
    virtual ~OutputStreamBase() = default;

    // Manually set the buffer mode, fails if the mode was already set (and different from buffer_mode)
    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) = 0;

    const LayerInfo& get_layer_info()
    {
        return m_layer_info;
    };

    const std::vector<hailo_quant_info_t> &get_quant_infos() const override
    {
        return m_quant_infos;
    }

    virtual hailo_status read(MemoryView buffer) override;
    virtual hailo_status read(void *buffer, size_t size) override;

    virtual hailo_status read_impl(MemoryView buffer) = 0;

    virtual hailo_status read_async(MemoryView buffer, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status read_async(void *buffer, size_t size, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status read_async(int dmabuf_fd, size_t size, const TransferDoneCallback &user_callback) override final;

    virtual hailo_status bind_buffer(TransferRequest &&transfer_request);

    virtual hailo_status read_async(TransferRequest &&transfer_request);
    virtual hailo_status read_unaligned_address_async(const MemoryView &buffer, const TransferDoneCallback &user_callback);

    virtual hailo_status abort() override final;
    virtual hailo_status abort_impl() = 0;

    virtual hailo_status clear_abort() override final;
    virtual hailo_status clear_abort_impl() = 0;

    virtual EventPtr &get_core_op_activated_event() override;
    virtual bool is_scheduled() override;

    virtual hailo_status activate_stream() = 0;
    virtual hailo_status deactivate_stream() = 0;

    virtual void set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle);

    virtual inline const char *get_device_id() { return ""; };
    // TODO - HRT-11739 - remove vdevice related members/functions (get/set_vdevice_core_op_handle)
    virtual inline vdevice_core_op_handle_t get_vdevice_core_op_handle() { return INVALID_CORE_OP_HANDLE; };

    virtual hailo_status cancel_pending_transfers();

protected:
    explicit OutputStreamBase(const LayerInfo &layer_info, EventPtr core_op_activated_event, hailo_status &status) :
        m_layer_info(layer_info), m_core_op_activated_event(std::move(core_op_activated_event))
    {
        const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
        assert(1 == stream_infos.size());
        m_stream_info = stream_infos[0];
        m_quant_infos = m_layer_info.quant_infos;
        status = HAILO_SUCCESS;
    }

    OutputStreamBase(const LayerInfo &layer_info, const hailo_stream_info_t &stream_info,
        const EventPtr &core_op_activated_event);

    LayerInfo m_layer_info;

private:
    EventPtr m_core_op_activated_event;
};

} /* namespace hailort */

#endif /* _STREAM_INTERNAL_HPP_ */
