/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file usb_transfer_queue.hpp
 * @brief Host-side transfer queue and ongoing USB transfer manager
 **/

#ifndef _USB_TRANSFER_QUEUE_HPP_
#define _USB_TRANSFER_QUEUE_HPP_

#include "hailo/hailort.h"
#include "vdma/transfer_common.hpp"

#include <libusb-1.0/libusb.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

namespace hailort
{

class UsbTransferQueue final
{
public:
    static Expected<std::unique_ptr<UsbTransferQueue>> create(libusb_device_handle *handle, uint8_t endpoint,
        const std::atomic_bool &is_closed);
    ~UsbTransferQueue();

    hailo_status wait_for_async_ready(std::chrono::milliseconds timeout);
    hailo_status enqueue_transfer(TransferRequest &&request);
    hailo_status cancel_pending_transfers();

private:
    struct OngoingTransfer {
        UsbTransferQueue *manager;
        TransferDoneCallback user_callback;
        TransferBuffers buffers;
        size_t current_buffer_index;
        size_t current_buffer_offset;
    };

    // All three must be called while holding m_queue_mutex.
    Expected<bool> submit_next_chunk(OngoingTransfer &transfer);
    void complete_ongoing(OngoingTransfer *transfer, hailo_status status,
        std::vector<std::pair<TransferDoneCallback, hailo_status>> &callbacks);
    void dispatch_pending(std::vector<std::pair<TransferDoneCallback, hailo_status>> &callbacks);
    void on_transfer_completed(libusb_transfer *transfer);
    static void LIBUSB_CALL bulk_transfer_callback(libusb_transfer *transfer);
    hailo_status convert_libusb_to_hailo_status(libusb_transfer *transfer) const;

    UsbTransferQueue(libusb_device_handle *handle, uint8_t endpoint, const std::atomic_bool &is_closed);

    Expected<libusb_transfer*> acquire_transfer_from_pool();
    void return_transfer_to_pool(libusb_transfer *transfer);

    static constexpr size_t MAX_PENDING_TRANSFERS = 128;
    static constexpr size_t TRANSFER_POOL_SIZE = 16;  // Pre-allocate 16 transfers

    libusb_device_handle *m_handle;
    const uint8_t m_endpoint;
    const std::atomic_bool &m_is_closed;

    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    std::queue<TransferRequest> m_transfer_queue;
    std::unique_ptr<OngoingTransfer> m_ongoing_context;
    libusb_transfer *m_ongoing_transfer = nullptr;
    
    std::vector<libusb_transfer *> m_transfer_pool;
    std::queue<libusb_transfer *> m_available_transfers;
};

} // namespace hailort

#endif // _USB_TRANSFER_QUEUE_HPP_
