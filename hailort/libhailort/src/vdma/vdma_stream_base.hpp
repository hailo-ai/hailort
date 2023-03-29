/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream_base.hpp
 * @brief Base class for stream objects over vDMA channel
 **/

#ifndef _HAILO_VDMA_STREAM_BASE_HPP_
#define _HAILO_VDMA_STREAM_BASE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "stream_common/stream_internal.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/channel/boundary_channel.hpp"


namespace hailort
{
constexpr std::chrono::seconds VDMA_FLUSH_TIMEOUT(10);

class VdmaInputStreamBase : public InputStreamBase {
public:
    static Expected<std::shared_ptr<VdmaInputStreamBase>> create(hailo_stream_interface_t interface,
        VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
        const hailo_stream_parameters_t &stream_params, uint16_t batch_size, EventPtr core_op_activated_event);

    virtual ~VdmaInputStreamBase();

    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual hailo_status flush() override;
    uint16_t get_dynamic_batch_size() const;
    const char* get_dev_id() const;
    Expected<vdma::BoundaryChannel::BufferState> get_buffer_state();
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;
    virtual hailo_status register_interrupt_callback(const vdma::ProcessingCompleteCallback &callback) override;

protected:
    VdmaInputStreamBase(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                    EventPtr core_op_activated_event, uint16_t batch_size,
                    std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t stream_interface,
                    hailo_status &status);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    virtual hailo_status deactivate_stream() override;
    hailo_status set_dynamic_batch_size(uint16_t dynamic_batch_size);

    VdmaDevice *m_device;
    vdma::BoundaryChannelPtr m_channel;
    const hailo_stream_interface_t m_interface;
    bool is_stream_activated;
    std::chrono::milliseconds m_channel_timeout;
    const uint16_t m_max_batch_size;
    uint16_t m_dynamic_batch_size;
};

class VdmaOutputStreamBase : public OutputStreamBase {
public:
    static Expected<std::shared_ptr<VdmaOutputStreamBase>> create(hailo_stream_interface_t interface,
        VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer, uint16_t batch_size, 
        const hailo_stream_parameters_t &stream_params, EventPtr core_op_activated_event);

    virtual ~VdmaOutputStreamBase();

    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    uint16_t get_dynamic_batch_size() const;
    const char* get_dev_id() const;
    Expected<vdma::BoundaryChannel::BufferState> get_buffer_state();
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;

    virtual hailo_status register_interrupt_callback(const vdma::ProcessingCompleteCallback &callback);

protected:
    VdmaOutputStreamBase(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                         EventPtr core_op_activated_event, uint16_t batch_size,
                         std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                         hailo_status &status);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    virtual hailo_status deactivate_stream() override;
    static uint32_t get_transfer_size(const hailo_stream_info_t &stream_info);
    hailo_status set_dynamic_batch_size(uint16_t dynamic_batch_size);

    VdmaDevice *m_device;
    vdma::BoundaryChannelPtr m_channel;
    const hailo_stream_interface_t m_interface;
    bool is_stream_activated;
    std::chrono::milliseconds m_transfer_timeout;
    const uint16_t m_max_batch_size;
    uint16_t m_dynamic_batch_size;
    const uint32_t m_transfer_size;
    std::mutex m_read_mutex;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_STREAM_BASE_HPP_ */
