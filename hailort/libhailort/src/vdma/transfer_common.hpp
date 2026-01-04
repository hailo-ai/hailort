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

#include "vdma/memory/mapped_buffer.hpp"
#include "common/os_utils.hpp"
#include "common/utils.hpp"

namespace hailort
{


class TransferBuffer final {
public:

    TransferBuffer() :
        m_dmabuf(),
        m_is_dmabuf(false),
        m_size(0),
        m_offset(0)
    {}

    TransferBuffer(hailo_dma_buffer_t dmabuf) :
        m_dmabuf(dmabuf),
        m_is_dmabuf(true),
        m_size(dmabuf.size),
        m_offset(0)
    {}

    TransferBuffer(MemoryView base_buffer, size_t size = 0, size_t offset = 0) :
        m_base_buffer(base_buffer),
        m_is_dmabuf(false),
        m_size(size ? size : base_buffer.size()),
        m_offset(offset)
    {
        assert(m_offset + m_size <= base_buffer.size());
    }

    Expected<MemoryView> base_buffer() const
    {
        CHECK(is_memview(), HAILO_INTERNAL_FAILURE, "base_buffer not supported for DMABUF");
        return Expected<MemoryView>(m_base_buffer);
    }

    Expected<int> dmabuf_fd() const
    {
        CHECK(is_dmabuf(), HAILO_INTERNAL_FAILURE, "dmabuf_fd not supported for MEMORYVIEW");
        return Expected<int>(m_dmabuf.fd);
    }

    size_t offset() const
    {
        return m_offset;
    }

    size_t size() const
    {
        return m_size;
    }

    uintptr_t addr_or_fd() const
    {
        if (is_memview()) {
            return reinterpret_cast<uintptr_t>(m_base_buffer.data() + m_offset);
        }
        return static_cast<uintptr_t>(m_dmabuf.fd);
    }

    hailo_status copy_to(MemoryView buffer) const
    {
        CHECK(buffer.size() == m_size, HAILO_INTERNAL_FAILURE, "buffer size {} must be {}", buffer.size(), m_size);
        CHECK(is_memview(), HAILO_INTERNAL_FAILURE, "copy_to not supported for DMABUF");
        memcpy(buffer.data(), m_base_buffer.data() + m_offset, m_size);
        return HAILO_SUCCESS;
    }

    hailo_status copy_from(const MemoryView buffer)
    {
        CHECK(buffer.size() == m_size, HAILO_INTERNAL_FAILURE, "buffer size {} must be {}", buffer.size(), m_size);
        CHECK(is_memview(), HAILO_INTERNAL_FAILURE, "copy_from only supported for MEMORYVIEW");
        memcpy(m_base_buffer.data() + m_offset, buffer.data(), m_size);
        return HAILO_SUCCESS;
    }

    bool is_aligned_for_dma() const
    {
        if (is_dmabuf()) {
            return true;
        }
        const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
        return (0 == reinterpret_cast<uintptr_t>(m_base_buffer.data()) % dma_able_alignment);
    }

    bool is_dmabuf() const
    {
        return m_is_dmabuf;
    }

    bool is_memview() const
    {
        return !m_is_dmabuf;
    }

private:

    union {
        MemoryView m_base_buffer;
        hailo_dma_buffer_t m_dmabuf;
    };

    bool m_is_dmabuf;
    size_t m_size;
    size_t m_offset;
};

// Internal function, wrapper to the user callbacks, accepts the callback status as an argument.
using TransferDoneCallback = std::function<void(hailo_status)>;

struct TransferRequest {
    // Initialization dependency - callback must be before transfer_buffers to avoid race condition
    TransferDoneCallback callback = [](hailo_status) {};
    std::vector<TransferBuffer> transfer_buffers;

    TransferRequest() = default;

    TransferRequest(TransferBuffer &&transfer_buffers_arg, const TransferDoneCallback &callback_arg):
        callback(callback_arg), transfer_buffers({ std::move(transfer_buffers_arg) })
    {}

    TransferRequest(std::vector<TransferBuffer> &&transfer_buffers_arg, const TransferDoneCallback &callback_arg) :
        callback(callback_arg), transfer_buffers(std::move(transfer_buffers_arg))
    {}

    size_t get_total_transfer_size() const {
        size_t total_transfer_size = 0;
        for (const auto &buffer : transfer_buffers) {
            total_transfer_size += buffer.size();
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
