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
 *     |-- VdmaInputStream
 *     |-- EthernetInputStream
 *     |-- MipiInputStream
 * 
 *
 * OutputStream                      (External "interface")
 * |-- OutputStreamBase              (Base class)
 *     |-- VdmaOutputStream
 *     |-- EthernetOutputStream
 * 
 **/

#ifndef _STREAM_INTERNAL_HPP_
#define _STREAM_INTERNAL_HPP_

#include "hailo/stream.hpp"
#include "hailo/event.hpp"
#include "hailo/hailort_common.hpp"

#include "hef/hef_internal.hpp"
#include "device_common/control_protocol.hpp"
#include "hef/layer_info.hpp"
#include "vdma/channel/boundary_channel.hpp"


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

class InputStreamWrapper;
class OutputStreamWrapper;

class InputStreamBase : public InputStream
{
public:
    virtual ~InputStreamBase() = default;

    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config()
    {
        return m_nn_stream_config;
    };

    virtual hailo_status send_pending_buffer(size_t device_index = 0)
    {
        (void)device_index;
        return HAILO_INVALID_OPERATION;
    }

    virtual Expected<size_t> get_buffer_frames_size() const
    {
        return make_unexpected(HAILO_INVALID_OPERATION);
    }
    
    virtual Expected<size_t> get_pending_frames_count() const
    {
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    virtual hailo_status register_interrupt_callback(const vdma::ProcessingCompleteCallback &/*callback*/)
    {
        return HAILO_INVALID_OPERATION;
    }

    CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;

protected:
    explicit InputStreamBase(const LayerInfo &layer_info, hailo_stream_interface_t stream_interface,
        EventPtr &&core_op_activated_event, hailo_status &status) :
        m_core_op_activated_event(std::move(core_op_activated_event))
    {
        m_stream_info = LayerInfoUtils::get_stream_info_from_layer_info(layer_info);

        const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer_info);
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

    InputStreamBase(const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &core_op_activated_event);

    virtual EventPtr &get_core_op_activated_event() override;
    virtual bool is_scheduled() override;

private:
    friend class InputStreamWrapper;

    EventPtr m_core_op_activated_event;
};


class OutputStreamBase : public OutputStream
{
public:
    virtual ~OutputStreamBase() = default;

    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config()
    {
        return m_nn_stream_config;
    };

    virtual const LayerInfo& get_layer_info() override
    {
        return m_layer_info;
    };

    virtual Expected<size_t> get_buffer_frames_size() const
    {
        return make_unexpected(HAILO_INVALID_OPERATION);
    }
    
    virtual Expected<size_t> get_pending_frames_count() const
    {
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    virtual hailo_status register_interrupt_callback(const vdma::ProcessingCompleteCallback &/*callback*/)
    {
        return HAILO_INVALID_OPERATION;
    }

    CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;

protected:
    explicit OutputStreamBase(const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event, hailo_status &status) :
        m_layer_info(layer_info), m_core_op_activated_event(std::move(core_op_activated_event))
    {
        m_stream_info = LayerInfoUtils::get_stream_info_from_layer_info(m_layer_info);

        const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(m_layer_info);
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

    virtual EventPtr &get_core_op_activated_event() override;
    virtual bool is_scheduled() override;

    LayerInfo m_layer_info;

private:
    friend class OutputStreamWrapper;

    EventPtr m_core_op_activated_event;
};

} /* namespace hailort */

#endif /* _STREAM_INTERNAL_HPP_ */
