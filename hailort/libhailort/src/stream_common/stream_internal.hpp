/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
 *     |-- VDeviceInputStreamMultiplexerWrapper
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
 *     |-- VDeviceOutputStreamMultiplexerWrapper
 **/

#ifndef _STREAM_INTERNAL_HPP_
#define _STREAM_INTERNAL_HPP_

#include "hailo/stream.hpp"
#include "hailo/event.hpp"
#include "hailo/hailort_common.hpp"

#include "stream_common/transfer_common.hpp"
#include "hef/hef_internal.hpp"
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

    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config()
    {
        return m_nn_stream_config;
    };

    const LayerInfo& get_layer_info()
    {
        return m_layer_info;
    };

    // Use by the scheduler to launch the transfer on the given activated device.
    // TODO HRT-11679: remove this.
    virtual hailo_status launch_transfer(const device_id_t &device_id)
    {
        (void)device_id;
        return HAILO_INVALID_OPERATION;
    }

    virtual Expected<size_t> get_buffer_frames_size() const
    {
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    const std::vector<hailo_quant_info_t> &get_quant_infos() const
    {
        return m_quant_infos;
    }

    virtual hailo_status write(const MemoryView &buffer) override final;
    virtual hailo_status write(const void *buffer, size_t size) override final;

    virtual hailo_status write_impl(const MemoryView &buffer) = 0;

    virtual hailo_status write_async(BufferPtr buffer, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status write_async(const MemoryView &buffer, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status write_async(const void *buffer, size_t size, const TransferDoneCallback &user_callback) override final;

    virtual hailo_status write_async(TransferRequest &&transfer_request);

    virtual EventPtr &get_core_op_activated_event() override;
    virtual bool is_scheduled() override;

    virtual hailo_status activate_stream() = 0;
    virtual hailo_status deactivate_stream() = 0;

    using ProcessingCompleteCallback = std::function<void()>;
    virtual hailo_status register_interrupt_callback(const ProcessingCompleteCallback &)
    {
        return HAILO_INVALID_OPERATION;
    }

    virtual void notify_all()
    {
        // Do nothing, override on subclass if notify is needed.
    }

    virtual vdevice_core_op_handle_t get_vdevice_core_op_handle();

    virtual void set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle);

    CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;

protected:
    explicit InputStreamBase(const LayerInfo &layer_info, hailo_stream_interface_t stream_interface,
        EventPtr core_op_activated_event, hailo_status &status) :
        m_layer_info(layer_info),
        m_core_op_activated_event(std::move(core_op_activated_event))
    {
        const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
        assert(1 == stream_infos.size());
        m_stream_info = stream_infos[0];
        m_quant_infos = layer_info.quant_infos;

        auto max_periph_bytes_from_hef = HefConfigurator::max_periph_bytes_value(stream_interface);
        if (HAILO_SUCCESS != max_periph_bytes_from_hef.status()) {
            status = max_periph_bytes_from_hef.status();
            return;
        }
        const auto max_periph_bytes = MIN(max_periph_bytes_from_hef.value(), layer_info.max_shmifo_size);
        const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer_info, max_periph_bytes);

        auto nn_stream_config = HefConfigurator::parse_nn_stream_config(layer_info,
            hw_padding_supported && (HAILO_STREAM_INTERFACE_MIPI != stream_interface)); // On MIPI networks, we don't want to use hw padding nn stream config.
        if(!nn_stream_config) {
            LOGGER__ERROR("Failed parse nn stream config");
            status = nn_stream_config.status();
            return;
        }
        m_nn_stream_config = nn_stream_config.release();
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

    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config()
    {
        return m_nn_stream_config;
    };

    const LayerInfo& get_layer_info()
    {
        return m_layer_info;
    };

    virtual Expected<size_t> get_buffer_frames_size() const
    {
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    const std::vector<hailo_quant_info_t> &get_quant_infos() const override
    {
        return m_quant_infos;
    }

    // Use by the scheduler to launch the transfer on the given activated device.
    // TODO HRT-11679: remove this.
    virtual hailo_status launch_transfer(const device_id_t &device_id)
    {
        (void)device_id;
        return HAILO_INVALID_OPERATION;
    }

    virtual hailo_status read(MemoryView buffer) override;
    virtual hailo_status read(void *buffer, size_t size) override;

    virtual hailo_status read_impl(MemoryView buffer) = 0;

    virtual hailo_status read_async(BufferPtr buffer, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status read_async(MemoryView buffer, const TransferDoneCallback &user_callback) override final;
    virtual hailo_status read_async(void *buffer, size_t size, const TransferDoneCallback &user_callback) override final;

    virtual hailo_status read_async(TransferRequest &&transfer_request);

    virtual EventPtr &get_core_op_activated_event() override;
    virtual bool is_scheduled() override;

    virtual hailo_status activate_stream() = 0;
    virtual hailo_status deactivate_stream() = 0;

    using ProcessingCompleteCallback = std::function<void()>;
    virtual hailo_status register_interrupt_callback(const ProcessingCompleteCallback &)
    {
        return HAILO_INVALID_OPERATION;
    }

    CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;

protected:
    explicit OutputStreamBase(const LayerInfo &layer_info, hailo_stream_interface_t stream_interface,
        EventPtr core_op_activated_event, hailo_status &status) :
        m_layer_info(layer_info), m_core_op_activated_event(std::move(core_op_activated_event))
    {
        const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
        assert(1 == stream_infos.size());
        m_stream_info = stream_infos[0];
        m_quant_infos = m_layer_info.quant_infos;

        auto max_periph_bytes_from_hef = HefConfigurator::max_periph_bytes_value(stream_interface);
        if (HAILO_SUCCESS != max_periph_bytes_from_hef.status()) {
            status = max_periph_bytes_from_hef.status();
            return;
        }
        const auto max_periph_bytes = MIN(max_periph_bytes_from_hef.value(), layer_info.max_shmifo_size);
        const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer_info, max_periph_bytes);

        auto nn_stream_config = HefConfigurator::parse_nn_stream_config(m_layer_info, hw_padding_supported);
        if(!nn_stream_config) {
            LOGGER__ERROR("Failed parse nn stream config");
            status = nn_stream_config.status();
            return;
        }
        m_nn_stream_config = nn_stream_config.release();
        status = HAILO_SUCCESS;
    }

    OutputStreamBase(const LayerInfo &layer_info, const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &core_op_activated_event);

    LayerInfo m_layer_info;

private:
    EventPtr m_core_op_activated_event;
};

} /* namespace hailort */

#endif /* _STREAM_INTERNAL_HPP_ */
