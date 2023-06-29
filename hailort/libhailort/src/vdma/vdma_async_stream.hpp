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
#include "vdevice/scheduler/scheduled_core_op_state.hpp"

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>


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

    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;

    virtual hailo_status write_buffer_only(const MemoryView &buffer, const std::function<bool()> &should_cancel) override;
    virtual hailo_status send_pending_buffer(const device_id_t &device_id) override;

    virtual hailo_status write_async(TransferRequest &&transfer_request) override;

protected:
    virtual hailo_status write_impl(const MemoryView &buffer) override;
};

class VdmaAsyncOutputStream : public VdmaOutputStreamBase
{
public:
    VdmaAsyncOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                          EventPtr core_op_activated_event, uint16_t batch_size,
                          std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                          hailo_status &status);
    virtual ~VdmaAsyncOutputStream()  = default;

    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;

protected:
    virtual hailo_status read_impl(MemoryView &buffer) override;
    virtual hailo_status read_async(TransferRequest &&transfer_request) override;
};

// NMS requires multiple reads from the device + parsing the output. Hence, a background thread is needed.
// This class opens a worker thread that processes nms transfers, signalling the user's callback upon completion.
// read_async adds transfer requests to a producer-consumer queue
class VdmaAsyncOutputNmsStream : public VdmaOutputStreamBase
{
public:
    VdmaAsyncOutputNmsStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                             EventPtr core_op_activated_event, uint16_t batch_size,
                             std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                             hailo_status &status);
    virtual ~VdmaAsyncOutputNmsStream();

    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual Expected<size_t> get_async_max_queue_size() const override;
    virtual hailo_status read(MemoryView buffer) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

private:
    virtual hailo_status read_impl(MemoryView &buffer) override;
    virtual hailo_status read_async(TransferRequest &&transfer_request) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_status activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) override;
    virtual Expected<size_t> get_buffer_frames_size() const override;

    void signal_thread_quit();
    void process_transfer_requests();

    // TODO: use SpscQueue (HRT-10554)
    const size_t m_queue_max_size;
    std::mutex m_queue_mutex;
    std::mutex m_abort_mutex;
    std::condition_variable m_queue_cond;
    std::queue<TransferRequest> m_queue;
    std::atomic_bool m_stream_aborted;
    // m_should_quit is used to quit the thread (called on destruction)
    bool m_should_quit;
    std::thread m_worker_thread;
};

} /* namespace hailort */

#endif /* _HAILO_VDMA_ASYNC_STREAM_HPP_ */
