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

    hailo_status write_buffer_only(const MemoryView &buffer, const std::function<bool()> &should_cancel = []() { return false; });
    hailo_status send_pending_buffer(size_t device_index = 0);

    void notify_all()
    {
        return m_channel->notify_all();
    }

private:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;

    std::mutex m_write_only_mutex;
    std::mutex m_send_pending_mutex;

    friend class InputVDeviceBaseStream;
    friend class InputVDeviceNativeStream;
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
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer);
    virtual hailo_status read_all(MemoryView &buffer) override;

    std::mutex m_read_mutex;

    friend class OutputVDeviceBaseStream;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_STREAM_HPP_ */
