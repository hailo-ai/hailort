/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_async_stream.cpp
 * @brief Async vdma stream implementation
 **/

#include "hailo/hailort_common.hpp"

#include "vdma/vdma_async_stream.hpp"
#include "common/os_utils.hpp"


namespace hailort
{

VdmaAsyncInputStream::VdmaAsyncInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                           const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                           uint16_t batch_size, std::chrono::milliseconds transfer_timeout,
                                           hailo_stream_interface_t stream_interface, hailo_status &status) :
    VdmaInputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size,
                        transfer_timeout, stream_interface, status)
{
    // Checking status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    if (channel->type() != vdma::BoundaryChannel::Type::ASYNC) {
        LOGGER__ERROR("Can't create a async vdma stream with a non async channel. Received channel type {}", channel->type());
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status VdmaAsyncInputStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    const bool STOP_IF_DEACTIVATED = true;
    return m_channel->wait(transfer_size, timeout, STOP_IF_DEACTIVATED);
}

Expected<size_t> VdmaAsyncInputStream::get_async_max_queue_size() const
{
    return get_buffer_frames_size();
}

hailo_status VdmaAsyncInputStream::write_buffer_only(const MemoryView &, const std::function<bool()> &)
{
    LOGGER__ERROR("The write_buffer_only function is not supported by async streams");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaAsyncInputStream::send_pending_buffer(const device_id_t &)
{
    LOGGER__ERROR("The send_pending_buffer function is not supported by async streams");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaAsyncInputStream::write_async(TransferRequest &&transfer_request)
{
    return m_channel->transfer_async(std::move(transfer_request));
}

hailo_status VdmaAsyncInputStream::write_impl(const MemoryView &)
{
    LOGGER__ERROR("Sync write is not supported by async streams");
    return HAILO_INVALID_OPERATION;
}

/** Output stream **/

VdmaAsyncOutputStream::VdmaAsyncOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                                             EventPtr core_op_activated_event, uint16_t batch_size,
                                             std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t interface,
                                             hailo_status &status) :
    VdmaOutputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size,
                         transfer_timeout, interface, status)
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    if (channel->type() != vdma::BoundaryChannel::Type::ASYNC) {
        LOGGER__ERROR("Can't create an async vdma stream with a non async channel. Received channel type {}", channel->type());
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status VdmaAsyncOutputStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    const bool STOP_IF_DEACTIVATED = true;
    return m_channel->wait(transfer_size, timeout, STOP_IF_DEACTIVATED);
}

Expected<size_t> VdmaAsyncOutputStream::get_async_max_queue_size() const
{
    return get_buffer_frames_size();
}

hailo_status VdmaAsyncOutputStream::read_impl(MemoryView &)
{
    LOGGER__ERROR("Sync read is not supported by async streams");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaAsyncOutputStream::read_async(TransferRequest &&transfer_request)
{
    return m_channel->transfer_async(std::move(transfer_request));
}

/** Output nms stream **/
VdmaAsyncOutputNmsStream::VdmaAsyncOutputNmsStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel,
                                                   const LayerInfo &edge_layer, EventPtr core_op_activated_event,
                                                   uint16_t batch_size, std::chrono::milliseconds transfer_timeout,
                                                   hailo_stream_interface_t interface, hailo_status &status) :
    VdmaOutputStreamBase(device, channel, edge_layer, core_op_activated_event, batch_size,
                         transfer_timeout, interface, status),
    m_queue_max_size(channel->get_transfers_count_in_buffer(get_info().hw_frame_size)),
    m_queue_mutex(),
    m_abort_mutex(),
    m_queue_cond(),
    m_queue(),
    m_stream_aborted(false),
    m_should_quit(false),
    m_worker_thread([this] { process_transfer_requests(); })
{
    // Check status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    if (edge_layer.format.order != HAILO_FORMAT_ORDER_HAILO_NMS) {
        // This shouldn't happen
        LOGGER__ERROR("Can't create NMS vdma async output stream if edge layer order isn't NMS. Order received {}",
            edge_layer.format.order);
        status = HAILO_INTERNAL_FAILURE;
        return;
    }

    // TODO: after adding NMS single int, we can create an async channel for async nms output stream (HRT-10553)
    if (channel->type() != vdma::BoundaryChannel::Type::BUFFERED) {
        LOGGER__ERROR("Can't create an async nms vdma stream with a non buffered channel. Received channel type {}", channel->type());
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    status = HAILO_SUCCESS;
}

VdmaAsyncOutputNmsStream::~VdmaAsyncOutputNmsStream()
{
    // VdmaAsyncOutputNmsStream::deactivate_stream() calls VdmaOutputStreamBase::deactivate_stream().
    // Because this dtor (i.e. ~VdmaAsyncOutputNmsStream()) is called before ~VdmaOutputStreamBase(), calling
    // VdmaOutputStreamBase::deactivate_stream() inside VdmaAsyncOutputNmsStream::deactivate_stream() will work.
    if (this->is_stream_activated) {
        const auto status = deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to deactivate stream with error status {}", status);
        }
    }

    if (m_worker_thread.joinable()) {
        signal_thread_quit();
        m_worker_thread.join();
    }
}

hailo_status VdmaAsyncOutputNmsStream::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    CHECK(transfer_size == get_info().hw_frame_size, HAILO_INSUFFICIENT_BUFFER,
        "On nms stream transfer_size should be {} (given size {})", get_info().hw_frame_size, transfer_size);
    std::unique_lock<std::mutex> lock(m_queue_mutex);
    auto result = m_queue_cond.wait_for(lock, timeout,
        [&]{ return m_should_quit || m_stream_aborted || (m_queue.size() < m_queue_max_size); });
    if (result) {
        if (m_should_quit) {
            return HAILO_STREAM_NOT_ACTIVATED;
        }
        return m_stream_aborted ? HAILO_STREAM_ABORTED_BY_USER : HAILO_SUCCESS;
    }
    return HAILO_TIMEOUT;
}

Expected<size_t> VdmaAsyncOutputNmsStream::get_async_max_queue_size() const
{
    return Expected<size_t>(m_queue_max_size);
}

hailo_status VdmaAsyncOutputNmsStream::read_async(TransferRequest &&transfer_request)
{
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        CHECK(!m_stream_aborted, HAILO_STREAM_ABORTED_BY_USER);
        CHECK(m_queue.size() < m_queue_max_size, HAILO_QUEUE_IS_FULL, "No space left in nms queue");

        m_queue.emplace(std::move(transfer_request));
    }
    m_queue_cond.notify_one();
    return HAILO_SUCCESS;
}

hailo_status VdmaAsyncOutputNmsStream::read(MemoryView /* buffer */)
{
    // We need to override read() since VdmaAsyncOutputNmsStream impl's read_impl. This will cause read() to succeed,
    // however this isn't desired for async streams.
    LOGGER__ERROR("The read function is not supported by async streams");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaAsyncOutputNmsStream::abort()
{
    std::unique_lock<std::mutex> lock(m_abort_mutex);
    const auto status = VdmaOutputStreamBase::abort();
    CHECK_SUCCESS(status);

    m_stream_aborted = true;

    return HAILO_SUCCESS;
}

hailo_status VdmaAsyncOutputNmsStream::clear_abort()
{
    std::unique_lock<std::mutex> lock(m_abort_mutex);
    const auto status = VdmaOutputStreamBase::clear_abort();
    CHECK_SUCCESS(status);

    m_stream_aborted = false;

    return HAILO_SUCCESS;
}

hailo_status VdmaAsyncOutputNmsStream::read_impl(MemoryView &buffer)
{
    CHECK((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
        "Size must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());

    return m_channel->transfer_sync(buffer.data(), buffer.size(), m_transfer_timeout);
}

hailo_status VdmaAsyncOutputNmsStream::deactivate_stream()
{
    std::unique_lock<std::mutex> lock(m_queue_mutex);

    // abort is called because read_nms may block on a non-aborted channel
    auto status = abort();
    CHECK_SUCCESS(status);

    // Now for every transfer processed in process_transfer_requests(), we'll pass HAILO_STREAM_ABORTED_BY_USER to the
    // callback.
    status = VdmaOutputStreamBase::deactivate_stream();
    CHECK_SUCCESS(status);

    // Block until all transfers have been emptied from the queue
    auto result = m_queue_cond.wait_for(lock, m_transfer_timeout, [&]{ return m_queue.empty(); });
    CHECK(result, HAILO_TIMEOUT, "Timeout while deactivating async nms output stream");

    return HAILO_SUCCESS;
}

hailo_status VdmaAsyncOutputNmsStream::activate_stream(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers)
{
    std::unique_lock<std::mutex> lock(m_queue_mutex);
    auto status = VdmaOutputStreamBase::activate_stream(dynamic_batch_size, resume_pending_stream_transfers);
    CHECK_SUCCESS(status);

    status = clear_abort();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<size_t> VdmaAsyncOutputNmsStream::get_buffer_frames_size() const
{
    return Expected<size_t>(m_queue_max_size);
}

void VdmaAsyncOutputNmsStream::signal_thread_quit()
{
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_should_quit = true;
    }
    m_queue_cond.notify_all();
}

void VdmaAsyncOutputNmsStream::process_transfer_requests()
{
    static const size_t FROM_START_OF_BUFFER = 0;
    OsUtils::set_current_thread_name("ASYNC_NMS");

    while (true) {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_queue_cond.wait(lock, [&]{ return m_should_quit || !m_queue.empty(); });
        if (m_should_quit) {
            break;
        }

        auto transfer_request = m_queue.front();
        m_queue.pop();

        lock.unlock();
        auto status = read_nms(transfer_request.buffer.data(), FROM_START_OF_BUFFER, transfer_request.buffer.size());
        lock.lock();

        if (!this->is_stream_activated) {
            LOGGER__TRACE("Stream is not active (previous status {})", status);
            transfer_request.callback(HAILO_STREAM_ABORTED_BY_USER);
        } else if (status != HAILO_SUCCESS) {
            // TODO: timeout? stream aborted? (HRT-10513)
            transfer_request.callback(status);
        } else {
            transfer_request.callback(HAILO_SUCCESS);
        }

        lock.unlock();

        // We notify after calling the callback, so that deactivate_stream() will block until the queue is empty + all callbacks have been called
        m_queue_cond.notify_one();
    }
}

} /* namespace hailort */
