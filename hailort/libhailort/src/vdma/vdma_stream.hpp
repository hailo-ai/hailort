/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.hpp
 * @brief Stream object over vDMA channel
 **/

#ifndef _HAILO_VDMA_STREAM_HPP_
#define _HAILO_VDMA_STREAM_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "stream_common/stream_internal.hpp"
#include "vdma/vdma_stream_base.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/channel/boundary_channel.hpp"
#include "vdevice/scheduler/scheduled_core_op_state.hpp"


namespace hailort
{

class VdmaInputStream : public VdmaInputStreamBase
{
public:
    VdmaInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                    EventPtr core_op_activated_event, uint16_t batch_size,
                    std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t stream_interface,
                    hailo_status &status);
    virtual ~VdmaInputStream() = default;

    virtual hailo_status write_buffer_only(const MemoryView &buffer, const std::function<bool()> &should_cancel = []() { return false; }) override;
    virtual hailo_status send_pending_buffer(const device_id_t &device_id) override;

private:
    virtual hailo_status write_impl(const MemoryView &buffer) override;

    std::mutex m_write_only_mutex;
    std::mutex m_send_pending_mutex;
};

class VdmaOutputStream : public VdmaOutputStreamBase
{
public:
    VdmaOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                     EventPtr core_op_activated_event, uint16_t batch_size,
                     std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                     hailo_status &status);
    virtual ~VdmaOutputStream() = default;

private:
    virtual hailo_status read_impl(MemoryView &buffer) override;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_STREAM_HPP_ */
