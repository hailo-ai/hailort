/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_transfer_queue.cpp
 * @brief Host-side transfer queue and in-flight USB transfer manager
 **/

#include "hrpc/session_internal/usb/usb_host/usb_transfer_queue.hpp"
#include "hrpc/session_internal/usb/usb_session.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

namespace hailort
{

Expected<std::unique_ptr<UsbTransferQueue>> UsbTransferQueue::create(libusb_device_handle *handle,
    uint8_t endpoint, const std::atomic_bool &is_closed)
{
    CHECK_NOT_NULL(handle, HAILO_INVALID_ARGUMENT);

    auto queue = std::unique_ptr<UsbTransferQueue>(new (std::nothrow) UsbTransferQueue(handle, endpoint, is_closed));
    CHECK_NOT_NULL(queue, HAILO_OUT_OF_HOST_MEMORY);

    // Pre-allocate transfer pool
    queue->m_transfer_pool.reserve(TRANSFER_POOL_SIZE);
    for (size_t i = 0; i < TRANSFER_POOL_SIZE; ++i) {
        libusb_transfer *transfer = libusb_alloc_transfer(0);
        CHECK_NOT_NULL(transfer, HAILO_OUT_OF_HOST_MEMORY);
        queue->m_transfer_pool.push_back(transfer);
        queue->m_available_transfers.push(transfer);
    }

    return queue;
}

UsbTransferQueue::UsbTransferQueue(libusb_device_handle *handle, uint8_t endpoint, const std::atomic_bool &is_closed) :
    m_handle(handle), m_endpoint(endpoint), m_is_closed(is_closed)
{}

UsbTransferQueue::~UsbTransferQueue()
{
    // Ensure no transfer is still in-ongoing before freeing pool memory.
    cancel_pending_transfers();

    for (libusb_transfer *transfer : m_transfer_pool) {
        libusb_free_transfer(transfer);
    }
}

Expected<libusb_transfer*> UsbTransferQueue::acquire_transfer_from_pool()
{
    if (!m_available_transfers.empty()) {
        libusb_transfer *transfer = m_available_transfers.front();
        m_available_transfers.pop();
        return transfer;
    }
    LOGGER__ERROR("No available USB transfers in pool");
    return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
}

void UsbTransferQueue::return_transfer_to_pool(libusb_transfer* transfer)
{
    if (nullptr == transfer) {
        return;
    }
    
    // Check if this transfer belongs to our pool
    bool is_pooled = false;
    for (libusb_transfer *pooled : m_transfer_pool) {
        if (pooled == transfer) {
            is_pooled = true;
            break;
        }
    }
    
    if (is_pooled) {
        m_available_transfers.push(transfer);
    } else {
        // Free non-pooled transfers
        libusb_free_transfer(transfer);
    }
}


hailo_status UsbTransferQueue::wait_for_async_ready(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_queue_mutex);
    const auto is_ready = [this]() {
        return m_is_closed.load() || (m_transfer_queue.size() < MAX_PENDING_TRANSFERS);
    };
    CHECK(m_queue_cv.wait_for(lock, timeout, is_ready), HAILO_TIMEOUT, "Timeout waiting for USB transfer queue");
    return HAILO_SUCCESS;
}

hailo_status UsbTransferQueue::enqueue_transfer(TransferRequest &&request)
{
    std::vector<std::pair<TransferDoneCallback, hailo_status>> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        CHECK(!m_is_closed.load(), HAILO_COMMUNICATION_CLOSED);
        const auto active = m_ongoing_context ? 1 : 0;
        CHECK((active + m_transfer_queue.size()) < MAX_PENDING_TRANSFERS,
            HAILO_QUEUE_IS_FULL, "USB queue is full (active={}, pending={})", active, m_transfer_queue.size());

        m_transfer_queue.push(std::move(request));
        dispatch_pending(callbacks);
    }

    for (const auto &callback : callbacks) {
        callback.first(callback.second);
    }
    return HAILO_SUCCESS;
}

Expected<bool> UsbTransferQueue::submit_next_chunk(OngoingTransfer &transfer)
{
    while (transfer.current_buffer_index < transfer.buffers.size()) {
        TRY(auto buffer, transfer.buffers[transfer.current_buffer_index].base_buffer());
        if (transfer.current_buffer_offset >= buffer.size()) {
            transfer.current_buffer_index++;
            transfer.current_buffer_offset = 0;
            continue;
        }

        const auto remaining = buffer.size() - transfer.current_buffer_offset;
        const auto chunk_size = std::min(USB_MAX_BUFFER_SIZE, remaining);
        TRY(libusb_transfer *libusb_transfer_ptr, acquire_transfer_from_pool());

        uint8_t *chunk_start = static_cast<uint8_t*>(buffer.data()) + transfer.current_buffer_offset;
        libusb_fill_bulk_transfer(libusb_transfer_ptr, m_handle, m_endpoint, chunk_start,
            static_cast<int>(chunk_size), bulk_transfer_callback, &transfer, 0);
        m_ongoing_transfer = libusb_transfer_ptr;

        const auto rc = libusb_submit_transfer(libusb_transfer_ptr);
        if (0 != rc) {
            m_ongoing_transfer = nullptr;
            return_transfer_to_pool(libusb_transfer_ptr);
            if (LIBUSB_ERROR_NO_DEVICE == rc) {
                LOGGER__INFO("USB device disconnected during transfer submit");
                return make_unexpected(HAILO_DEVICE_NOT_CONNECTED);
            }
            LOGGER__ERROR("Failed to submit USB transfer: {}", libusb_error_name(rc));
            return make_unexpected(HAILO_LIBUSB_FAILURE);
        }

        return true;
    }

    return false;
}

void UsbTransferQueue::complete_ongoing(OngoingTransfer *transfer, hailo_status status,
    std::vector<std::pair<TransferDoneCallback, hailo_status>> &callbacks)
{
    if (!m_ongoing_context || (m_ongoing_context.get() != transfer)) {
        return;
    }

    if (m_ongoing_context->user_callback) {
        callbacks.emplace_back(std::move(m_ongoing_context->user_callback), status);
    }
    m_ongoing_context.reset();
    m_queue_cv.notify_all();
}

// Submits the next queued transfer request to USB. Only one transfer can be in-ongoing at a time.
void UsbTransferQueue::dispatch_pending(std::vector<std::pair<TransferDoneCallback, hailo_status>> &callbacks)
{
    while (!m_is_closed.load() && !m_transfer_queue.empty() && (nullptr == m_ongoing_context)) {
        auto pending = std::move(m_transfer_queue.front());
        m_transfer_queue.pop();

        auto saved_callback = pending.callback;

        m_ongoing_context = make_unique_nothrow<OngoingTransfer>(OngoingTransfer{
            this, std::move(pending.callback), std::move(pending.transfer_buffers), 0, 0  
        });
        if (nullptr == m_ongoing_context) {
            if (saved_callback) {
                callbacks.emplace_back(std::move(saved_callback), HAILO_OUT_OF_HOST_MEMORY);
            }
            m_queue_cv.notify_all();
            continue;
        }

        auto expected_submitted = submit_next_chunk(*m_ongoing_context);
        if (!expected_submitted) {
            complete_ongoing(m_ongoing_context.get(), expected_submitted.status(), callbacks);
            continue;
        }

        if (!expected_submitted.release()) {
            complete_ongoing(m_ongoing_context.get(), HAILO_SUCCESS, callbacks);
        }
    }
}

void LIBUSB_CALL UsbTransferQueue::bulk_transfer_callback(libusb_transfer *transfer)
{
    OngoingTransfer *context = static_cast<OngoingTransfer*>(transfer->user_data);
    if (nullptr == context) {
        libusb_free_transfer(transfer);
        return;
    }
    context->manager->on_transfer_completed(transfer);
}

hailo_status UsbTransferQueue::convert_libusb_to_hailo_status(libusb_transfer *transfer) const
{
    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        // Zero-length on an IN (read) endpoint means no data was received — treat as error.
        // Zero-length on an OUT (write) endpoint is a valid Zero-Length Packet (ZLP).
        if ((0 == transfer->actual_length) &&
            ((transfer->endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)) {
            return HAILO_LIBUSB_FAILURE;
        }
        return HAILO_SUCCESS;
    case LIBUSB_TRANSFER_TIMED_OUT:
        return HAILO_TIMEOUT;
    case LIBUSB_TRANSFER_CANCELLED:
    case LIBUSB_TRANSFER_NO_DEVICE:
        return HAILO_COMMUNICATION_CLOSED;
    default:
        return (m_is_closed.load() ? HAILO_COMMUNICATION_CLOSED : HAILO_LIBUSB_FAILURE);
    }
}

void UsbTransferQueue::on_transfer_completed(libusb_transfer *transfer)
{
    // Local vector — avoids the data race that a member variable would cause with enqueue_transfer.
    std::vector<std::pair<TransferDoneCallback, hailo_status>> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);

        OngoingTransfer *context = static_cast<OngoingTransfer*>(transfer->user_data);
        if (nullptr == context) {
            return_transfer_to_pool(transfer);
            return;
        }
        m_ongoing_transfer = nullptr;

        const auto status = convert_libusb_to_hailo_status(transfer);

        if (HAILO_SUCCESS == status) {
            context->current_buffer_offset += static_cast<size_t>(transfer->actual_length);
        }

        return_transfer_to_pool(transfer);

        if (HAILO_SUCCESS != status) {
            complete_ongoing(context, status, callbacks);
            dispatch_pending(callbacks);
        } else {
            auto expected_submitted = submit_next_chunk(*context);
            if (!expected_submitted) {
                complete_ongoing(context, expected_submitted.status(), callbacks);
                dispatch_pending(callbacks);
            } else if (!expected_submitted.release()) {
                complete_ongoing(context, HAILO_SUCCESS, callbacks);
                dispatch_pending(callbacks);
            }
        }
    }

    for (const auto &callback : callbacks) {
        callback.first(callback.second);
    }
}

hailo_status UsbTransferQueue::cancel_pending_transfers()
{
    std::vector<std::pair<TransferDoneCallback, hailo_status>> callbacks_to_run;
    auto status = HAILO_SUCCESS;

    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        while (!m_transfer_queue.empty()) {
            auto pending = std::move(m_transfer_queue.front());
            m_transfer_queue.pop();
            if (pending.callback) {
                callbacks_to_run.emplace_back(std::move(pending.callback), HAILO_COMMUNICATION_CLOSED);
            }
        }

        if (nullptr != m_ongoing_transfer) {
            const auto rc = libusb_cancel_transfer(m_ongoing_transfer);
            if ((0 != rc) && (LIBUSB_ERROR_NOT_FOUND != rc)) {
                LOGGER__ERROR("Failed to cancel USB transfer: {}", libusb_error_name(rc));
                status = HAILO_LIBUSB_FAILURE;
            }

            m_queue_cv.wait(lock, [this]() {
                return (nullptr == m_ongoing_context);
            });
        }

        m_queue_cv.notify_all();
    }

    for (const auto &callback : callbacks_to_run) {
        callback.first(callback.second);
    }

    return status;
}

} // namespace hailort
