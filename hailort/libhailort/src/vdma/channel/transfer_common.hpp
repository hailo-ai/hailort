/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file transfer_common.hpp
 * @brief Common types/functions for async api
 **/

#ifndef _HAILO_TRANSFER_COMMON_HPP_
#define _HAILO_TRANSFER_COMMON_HPP_

#include "hailo/stream.hpp"
#include "hailo/buffer.hpp"

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/mapped_buffer.hpp"
#include "common/os_utils.hpp"

namespace hailort
{

enum class TransferBufferType {
    MEMORYVIEW = 0,
    DMABUF
};

// Contains buffer that can be transferred. The buffer can be circular -
// It relies at [m_offset, m_base_buffer.size()) and [0, m_base_buffer.size() - m_size).
class TransferBuffer final {
public:

    TransferBuffer();
    TransferBuffer(hailo_dma_buffer_t dmabuf);
    TransferBuffer(MemoryView base_buffer);
    TransferBuffer(MemoryView base_buffer, size_t size, size_t offset);

    Expected<MemoryView> base_buffer();
    Expected<int> dmabuf_fd();

    size_t offset() const { return m_offset; }
    size_t size() const { return m_size; }

    Expected<vdma::MappedBufferPtr> map_buffer(HailoRTDriver &driver, HailoRTDriver::DmaDirection direction);
    void unmap_buffer();

    // Assumes map_buffer() has already been called.
    // Returns the transfer-buffer as the driver expects to receive it.
    // If the buffer is circular with wrap-around, will return two buffers
    // similar to get_continuos_parts(). Otherwise, we return only one.
    Expected<std::vector<HailoRTDriver::TransferBuffer>> to_driver_buffers();

    hailo_status copy_to(MemoryView buffer);
    hailo_status copy_from(const MemoryView buffer);

    bool is_aligned_for_dma() const;

    TransferBufferType type () const { return m_type; }

private:

    bool is_wrap_around() const;

    // Returns the continuous parts of the buffer.
    // There are 2 cases:
    //      1. If the buffer is_wrap_around(), both parts are valid, the first one starts at m_offset until
    //         m_base_buffer end
    //         The second part is the residue, starting from offset 0.
    //      2. If the buffer is not circular, the first part will contain the buffer, the second will point to nullptr.
    std::pair<MemoryView, MemoryView> get_continuous_parts();

    union {
        MemoryView m_base_buffer;
        hailo_dma_buffer_t m_dmabuf;
    };

    size_t m_size;
    size_t m_offset;
    TransferBufferType m_type;

    // Once map_buffer is called, a MappedBuffer object is stored here to make sure the buffer is mapped.
    vdma::MappedBufferPtr m_mappings;
};

// Internal function, wrapper to the user callbacks, accepts the callback status as an argument.
using TransferDoneCallback = std::function<void(hailo_status)>;

struct TransferRequest {
    // Initialization dependency - callback must be before transfer_buffers to avoid race condition
    TransferDoneCallback callback = [](hailo_status) {};
    std::vector<TransferBuffer> transfer_buffers;
    TransferRequest() = default;
    TransferRequest(TransferBuffer &&transfer_buffers_arg, const TransferDoneCallback &callback_arg):
        callback(callback_arg), transfer_buffers()
    {
        transfer_buffers.emplace_back(std::move(transfer_buffers_arg));
    }
    TransferRequest(const TransferBuffer& transfer_buffers_arg, const TransferDoneCallback &callback_arg):
        callback(callback_arg), transfer_buffers()
    {
        transfer_buffers.emplace_back(std::move(transfer_buffers_arg));
    }
    TransferRequest(std::vector<TransferBuffer> &&transfer_buffers_arg, const TransferDoneCallback &callback_arg) :
        callback(callback_arg), transfer_buffers(std::move(transfer_buffers_arg))
    {}

    size_t get_total_transfer_size() const {
        size_t total_transfer_size = 0;
        for (size_t i = 0; i < transfer_buffers.size(); i++) {
            total_transfer_size += transfer_buffers[i].size();
        }
        return total_transfer_size;
    }

    Expected<bool> is_request_end_aligned() {
        const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
        TRY(auto base_buffer, transfer_buffers[0].base_buffer());
        const auto buffer_size = transfer_buffers[0].size();
        return !((reinterpret_cast<uintptr_t>(base_buffer.data()) + buffer_size) % dma_able_alignment);
    }
};

// Helper function to create TransferRequest from buffer and callback
inline TransferRequest to_request(void *buffer, size_t size, const TransferDoneCallback &callback) {
    return TransferRequest(TransferBuffer(MemoryView(buffer, size)), callback);
}

struct InferRequest {
    // Transfer for each stream
    std::unordered_map<std::string, TransferRequest> transfers;

    // Callback to be called when all transfer finishes
    TransferDoneCallback callback;
};

} /* namespace hailort */

#endif /* _HAILO_TRANSFER_COMMON_HPP_ */
