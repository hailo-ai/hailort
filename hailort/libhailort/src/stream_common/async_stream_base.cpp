/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_stream_base.cpp
 **/

#include "async_stream_base.hpp"
#include "common/os_utils.hpp"

namespace hailort
{

// Currently there is 1-1 relation between buffer mode and api (sync vs async).
// This function returns the API name for the buffer mode for better user logging.
static const char *get_buffer_mode_api_name(StreamBufferMode mode)
{
    switch (mode) {
    case StreamBufferMode::OWNING:
        return "Sync";
    case StreamBufferMode::NOT_OWNING:
        return "Async";
    case StreamBufferMode::NOT_SET:
        return "Unset";
    default:
        return "Unknown";
    }
}

AsyncInputStreamBase::AsyncInputStreamBase(const LayerInfo &edge_layer, EventPtr core_op_activated_event,
    hailo_status &status) :
        InputStreamBase(edge_layer, core_op_activated_event, status),
        m_is_stream_activated(false),
        m_is_aborted(false),
        m_timeout(DEFAULT_TRANSFER_TIMEOUT),
        m_buffer_mode(StreamBufferMode::NOT_SET),
        m_ongoing_transfers(0)
{
    // Checking status for base class c'tor
    if (HAILO_SUCCESS != status) {
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status AsyncInputStreamBase::abort_impl()
{
    {
        std::lock_guard<std::mutex> lock(m_stream_mutex);
        m_is_aborted = true;
    }
    m_has_ready_buffer.notify_all();
    return HAILO_SUCCESS;
}

hailo_status AsyncInputStreamBase::clear_abort_impl()
{
    {
        std::lock_guard<std::mutex> lock(m_stream_mutex);
        m_is_aborted = false;
    }

    return HAILO_SUCCESS;
}

hailo_status AsyncInputStreamBase::set_buffer_mode(StreamBufferMode buffer_mode)
{
    CHECK(StreamBufferMode::NOT_SET != buffer_mode, HAILO_INVALID_OPERATION, "Can't set buffer mode to NOT_SET");

    std::unique_lock<std::mutex> lock(m_stream_mutex);
    if (m_buffer_mode == buffer_mode) {
        // Nothing to be done
        return HAILO_SUCCESS;
    }

    CHECK(StreamBufferMode::NOT_SET == m_buffer_mode, HAILO_INVALID_OPERATION, "Invalid {} operation on {} stream",
        get_buffer_mode_api_name(buffer_mode), get_buffer_mode_api_name(m_buffer_mode));
    m_buffer_mode = buffer_mode;

    if (buffer_mode == StreamBufferMode::OWNING) {
        assert(m_buffer_pool == nullptr);
        auto buffer_pool = allocate_buffer_pool();
        CHECK_EXPECTED_AS_STATUS(buffer_pool);
        m_buffer_pool = buffer_pool.release();
    }

    return HAILO_SUCCESS;
}

std::chrono::milliseconds AsyncInputStreamBase::get_timeout() const
{
    return m_timeout;
}

hailo_status AsyncInputStreamBase::set_timeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
    return HAILO_SUCCESS;
}

hailo_status AsyncInputStreamBase::flush()
{
    std::unique_lock<std::mutex> lock(m_stream_mutex);

    if (0 == m_ongoing_transfers) {
        return HAILO_SUCCESS;
    }

    const auto flush_timeout = m_ongoing_transfers.load() * m_timeout;
    return cv_wait_for(lock, flush_timeout, [this]() {
        return m_ongoing_transfers == 0;
    });
}

hailo_status AsyncInputStreamBase::write_impl(const MemoryView &user_buffer)
{
    auto status = set_buffer_mode(StreamBufferMode::OWNING);
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_stream_mutex);
    auto is_ready = [this]() { return is_ready_for_transfer() && is_ready_for_dequeue(); };
    status = cv_wait_for(lock, m_timeout, is_ready);
    if (HAILO_SUCCESS != status) {
        // errors logs on cv_wait_for
        return status;
    }

    auto stream_buffer_exp = m_buffer_pool->dequeue();
    CHECK_EXPECTED_AS_STATUS(stream_buffer_exp);
    auto stream_buffer = stream_buffer_exp.release();

    status = stream_buffer.copy_from(user_buffer);
    CHECK_SUCCESS(status);

    return call_write_async_impl(TransferRequest(std::move(stream_buffer),
        [this, stream_buffer](hailo_status) {
            std::unique_lock<std::mutex> lock(m_stream_mutex);
            auto enqueue_status = m_buffer_pool->enqueue(TransferBuffer{stream_buffer});
            if (HAILO_SUCCESS != enqueue_status) {
                LOGGER__ERROR("Failed enqueue stream buffer {}", enqueue_status);
            }
        }
    ));
}

Expected<size_t> AsyncInputStreamBase::get_async_max_queue_size() const
{
    return get_max_ongoing_transfers();
}

hailo_status AsyncInputStreamBase::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    auto status = set_buffer_mode(StreamBufferMode::NOT_OWNING);
    CHECK_SUCCESS(status);

    CHECK(transfer_size == get_frame_size(), HAILO_INVALID_OPERATION, "transfer size {} is expected to be {}",
        transfer_size, get_frame_size());

    std::unique_lock<std::mutex> lock(m_stream_mutex);
    return cv_wait_for(lock, timeout, [this]() {
        return is_ready_for_transfer();
    });
}

hailo_status AsyncInputStreamBase::write_async(TransferRequest &&transfer_request)
{
    auto status = set_buffer_mode(StreamBufferMode::NOT_OWNING);
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_stream_mutex);

    if (m_is_aborted) {
        return HAILO_STREAM_ABORT;
    } else if (!m_is_stream_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    return call_write_async_impl(std::move(transfer_request));
}

hailo_status AsyncInputStreamBase::activate_stream()
{
    std::unique_lock<std::mutex> lock(m_stream_mutex);

    // Clear old abort state
    m_is_aborted = false;

    auto status = activate_stream_impl();
    CHECK_SUCCESS(status);

    // If the mode is OWNING is set, it means we use the write/write_impl API. We want to make sure the buffer starts
    // from the beginning of the buffer pool (to avoid unnecessary buffer bindings).
    if (StreamBufferMode::OWNING == m_buffer_mode) {
        m_buffer_pool->reset_pointers();
    }

    m_is_stream_activated = true;

    return HAILO_SUCCESS;
}

hailo_status AsyncInputStreamBase::deactivate_stream()
{
    hailo_status status = HAILO_SUCCESS; // success oriented

    {
        std::unique_lock<std::mutex> lock(m_stream_mutex);

        if (!m_is_stream_activated) {
            return HAILO_SUCCESS;
        }

        auto deactivate_channel_status = deactivate_stream_impl();
        if (HAILO_SUCCESS != deactivate_channel_status) {
            LOGGER__ERROR("Failed to stop channel with status {}", deactivate_channel_status);
            status = deactivate_channel_status;
        }

        m_is_stream_activated = false;
    }
    m_has_ready_buffer.notify_all();

    return status;
}

hailo_status AsyncInputStreamBase::call_write_async_impl(TransferRequest &&transfer_request)
{
    transfer_request.callback = [this, callback=transfer_request.callback](hailo_status callback_status) {
        callback(callback_status);

        {
            std::lock_guard<std::mutex> lock(m_stream_mutex);
            m_ongoing_transfers--;

            if (HAILO_SUCCESS != callback_status) {
                if (m_is_stream_activated) {
                    // Need to abort only if we are active!
                    m_is_aborted = true;
                }
            }
        }

        m_has_ready_buffer.notify_all();
    };


    auto status = write_async_impl(std::move(transfer_request));
    if ((HAILO_STREAM_NOT_ACTIVATED == status) || (HAILO_STREAM_ABORT == status)) {
        return status;
    }
    CHECK_SUCCESS(status);

    m_ongoing_transfers++;

    return HAILO_SUCCESS;
}

bool AsyncInputStreamBase::is_ready_for_transfer() const
{
    return m_ongoing_transfers < get_max_ongoing_transfers();
}

bool AsyncInputStreamBase::is_ready_for_dequeue() const
{
    return m_ongoing_transfers < m_buffer_pool->max_queue_size();
}

AsyncOutputStreamBase::AsyncOutputStreamBase(const LayerInfo &edge_layer, EventPtr core_op_activated_event,
    hailo_status &status) :
        OutputStreamBase(edge_layer, std::move(core_op_activated_event), status),
        m_is_stream_activated(false),
        m_is_aborted(false),
        m_timeout(DEFAULT_TRANSFER_TIMEOUT),
        m_buffer_mode(StreamBufferMode::NOT_SET),
        m_ongoing_transfers(0)
{}

hailo_status AsyncOutputStreamBase::abort_impl()
{
    {
        std::lock_guard<std::mutex> lock(m_stream_mutex);
        m_is_aborted = true;
    }
    m_has_ready_buffer.notify_all();
    return HAILO_SUCCESS;
}

hailo_status AsyncOutputStreamBase::clear_abort_impl()
{
    {
        std::lock_guard<std::mutex> lock(m_stream_mutex);
        m_is_aborted = false;
    }
    return HAILO_SUCCESS;
}

hailo_status AsyncOutputStreamBase::wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout)
{
    auto status = set_buffer_mode(StreamBufferMode::NOT_OWNING);
    CHECK_SUCCESS(status);

    CHECK(transfer_size == get_frame_size(), HAILO_INVALID_OPERATION, "transfer size {} is expected to be {}",
        transfer_size, get_frame_size());

    std::unique_lock<std::mutex> lock(m_stream_mutex);
    return cv_wait_for(lock, timeout, [this]() {
        return is_ready_for_transfer();
    });
}

Expected<size_t> AsyncOutputStreamBase::get_async_max_queue_size() const
{
    return get_max_ongoing_transfers();
}

hailo_status AsyncOutputStreamBase::read_async(TransferRequest &&transfer_request)
{
    auto status = set_buffer_mode(StreamBufferMode::NOT_OWNING);
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_stream_mutex);

    if (m_is_aborted) {
        return HAILO_STREAM_ABORT;
    } else if (!m_is_stream_activated) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    return call_read_async_impl(std::move(transfer_request));
}

hailo_status AsyncOutputStreamBase::call_read_async_impl(TransferRequest &&transfer_request)
{
    transfer_request.callback = [this, callback=transfer_request.callback](hailo_status callback_status) {
        callback(callback_status);

        {
            std::lock_guard<std::mutex> lock(m_stream_mutex);
            m_ongoing_transfers--;

            if (HAILO_SUCCESS != callback_status) {
                if (m_is_stream_activated) {
                    // Need to abort only if we are active!
                    m_is_aborted = true;
                }
            }
        }

        m_has_ready_buffer.notify_all();
    };

    auto status = read_async_impl(std::move(transfer_request));
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    m_ongoing_transfers++;

    return HAILO_SUCCESS;
}

hailo_status AsyncOutputStreamBase::activate_stream()
{
    std::unique_lock<std::mutex> lock(m_stream_mutex);

    // Clear old abort state
    m_is_aborted = false;

    auto status = activate_stream_impl();
    CHECK_SUCCESS(status);

    // If the mode is OWNING is set, it means we use the read/read_impl API.
    // We need to clear all pending buffers, and prepare transfers for next read requests.
    if (StreamBufferMode::OWNING == m_buffer_mode) {
        m_pending_buffers.clear();
        m_buffer_pool->reset_pointers();

        status = prepare_all_transfers();
        CHECK_SUCCESS(status);
    }

    m_is_stream_activated = true;
    return HAILO_SUCCESS;
}

hailo_status AsyncOutputStreamBase::deactivate_stream()
{
    hailo_status status = HAILO_SUCCESS; // success oriented

    {
        std::unique_lock<std::mutex> lock(m_stream_mutex);

        if (!m_is_stream_activated) {
            return HAILO_SUCCESS;
        }

        m_is_stream_activated = false;

        auto deactivate_status = deactivate_stream_impl();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to stop stream with status {}", deactivate_status);
            status = deactivate_status;
        }
    }
    m_has_ready_buffer.notify_all();

    return status;
}

bool AsyncOutputStreamBase::is_ready_for_transfer() const
{
    return m_ongoing_transfers < get_max_ongoing_transfers();
}

hailo_status AsyncOutputStreamBase::prepare_all_transfers()
{
    const auto queue_size = get_max_ongoing_transfers();
    for (size_t i = 0; i < queue_size; i++) {
        auto status = dequeue_and_launch_transfer();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status AsyncOutputStreamBase::set_buffer_mode(StreamBufferMode buffer_mode)
{
    CHECK(StreamBufferMode::NOT_SET != buffer_mode, HAILO_INVALID_OPERATION, "Can't set buffer mode to NOT_SET");

    std::unique_lock<std::mutex> lock(m_stream_mutex);
    if (m_buffer_mode == buffer_mode) {
        // Nothing to be done
        return HAILO_SUCCESS;
    }

    CHECK(StreamBufferMode::NOT_SET == m_buffer_mode, HAILO_INVALID_OPERATION, "Invalid {} operation on {} stream",
        get_buffer_mode_api_name(buffer_mode), get_buffer_mode_api_name(m_buffer_mode));
    m_buffer_mode = buffer_mode;

    if (buffer_mode == StreamBufferMode::OWNING) {
        assert(m_buffer_pool == nullptr);
        auto buffer_pool = allocate_buffer_pool();
        CHECK_EXPECTED_AS_STATUS(buffer_pool);
        m_buffer_pool = buffer_pool.release();

        if (m_is_stream_activated) {
            // if the streams are not activated, the transfers will be prepared on next activation.
            auto status = prepare_all_transfers();
            CHECK_SUCCESS(status);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status AsyncOutputStreamBase::set_timeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
    return HAILO_SUCCESS;
}

std::chrono::milliseconds AsyncOutputStreamBase::get_timeout() const
{
    return m_timeout;
}

hailo_status AsyncOutputStreamBase::read_impl(MemoryView user_buffer)
{
    auto status = set_buffer_mode(StreamBufferMode::OWNING);
    CHECK_SUCCESS(status);

    // Dequeue pending buffer, read it into user_buffer and return the buffer back to the pool.
    std::unique_lock<std::mutex> lock(m_stream_mutex);
    status = cv_wait_for(lock, m_timeout, [this]() { return !m_pending_buffers.empty(); });
    if (HAILO_SUCCESS != status) {
        // errors logs on cv_wait_for
        return status;
    }

    auto stream_buffer = m_pending_buffers.dequeue();
    CHECK_EXPECTED_AS_STATUS(stream_buffer);

    status = stream_buffer->copy_to(user_buffer);
    CHECK_SUCCESS(status);

    status = m_buffer_pool->enqueue(stream_buffer.release());
    CHECK_SUCCESS(status);

    status = dequeue_and_launch_transfer();
    if (HAILO_STREAM_ABORT == status) {
        // The buffer_pool state will reset on next activation.
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status AsyncOutputStreamBase::dequeue_and_launch_transfer()
{
    auto buffer = m_buffer_pool->dequeue();
    CHECK_EXPECTED_AS_STATUS(buffer);

    auto callback = [this, buffer=buffer.value()](hailo_status status) {
        if (HAILO_STREAM_ABORT == status) {
            // On deactivation flow, we should get this status. We just ignore the callback here, and in the next
            // activation we should reset the buffers.
            return;
        }

        status = m_pending_buffers.enqueue(TransferBuffer{buffer});
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to enqueue pending buffer {}", status);
        }
    };

    auto status = call_read_async_impl(TransferRequest(std::move(buffer.value()), callback));
    if (HAILO_STREAM_ABORT == status) {
        // The buffer_pool state will reset on next activation.
        return status;
    }
    CHECK_SUCCESS(status, "Fatal error {} while launching transfer. state may be corrupted", status);

    return HAILO_SUCCESS;
}

} /* namespace hailort */
