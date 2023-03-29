/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.hpp
 * @brief Async stream object over vDMA channel
 **/

#ifndef _HAILO_VDMA_ASYNC_STREAM_HPP_
#define _HAILO_VDMA_ASYNC_STREAM_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/stream.hpp"

#include "vdma/vdma_stream_base.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/channel/async_channel.hpp"


namespace hailort
{

class VdmaAsyncInputStream : public VdmaInputStreamBase
{
public:
    VdmaAsyncInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                         EventPtr core_op_activated_event, uint16_t batch_size,
                         std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t stream_interface,
                         hailo_status &status);
    virtual ~VdmaAsyncInputStream() = default;

    virtual hailo_status wait_for_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque);

private:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;
};

class VdmaAsyncOutputStream : public VdmaOutputStreamBase
{
public:
    VdmaAsyncOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                          EventPtr core_op_activated_event, uint16_t batch_size,
                          std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                          hailo_status &status);
    virtual ~VdmaAsyncOutputStream()  = default;

    virtual hailo_status wait_for_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status read_async(std::shared_ptr<DmaMappedBuffer> buffer, const TransferDoneCallback &user_callback, void *opaque = nullptr) override;

private:
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer);
    virtual hailo_status read_all(MemoryView &buffer) override;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_ASYNC_STREAM_HPP_ */
