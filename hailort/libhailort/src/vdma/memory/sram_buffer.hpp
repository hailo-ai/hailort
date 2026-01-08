/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sram_buffer.hpp
 * @brief Sysram-buffer used for DDR-portal optimizations on Pluto.
 **/

#ifndef _HAILO_VDMA_SRAM_BUFFER_HPP_
#define _HAILO_VDMA_SRAM_BUFFER_HPP_

#include "vdma/memory/vdma_buffer.hpp"


namespace hailort {
namespace vdma {

class SramBuffer final : public ContinuousVdmaBuffer {
public:

    friend class SramBufferAllocator;

    SramBuffer(const SramBuffer &) = delete;
    SramBuffer& operator=(const SramBuffer &) = delete;
    SramBuffer& operator=(SramBuffer &&) = delete;

    virtual ~SramBuffer() = default;

    SramBuffer(SramBuffer &&other) noexcept :
        ContinuousVdmaBuffer(std::move(other)), m_size(other.m_size), m_dma_address(other.dma_address())
    {}

    virtual BufferType type() const override
    {
        return BufferType::SRAM;
    }

    virtual size_t size() const override
    {
        return m_size;
    }

    virtual hailo_status read(void*, size_t, size_t) override
    {
        LOGGER__WARN("SRAM buffer-type does not support read() operations");
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status write(const void*, size_t, size_t) override
    {
        LOGGER__WARN("SRAM buffer-type does not support write() operations");
        return HAILO_NOT_SUPPORTED;
    }

    uint64_t dma_address() const override
    {
        return m_dma_address;
    }

private:
    SramBuffer(size_t size, uint64_t addr) noexcept :
        m_size(size), m_dma_address(addr)
    {}

    size_t m_size;
    uint64_t m_dma_address;
};

class SramBufferAllocator final {
public:
    SramBufferAllocator() noexcept = default;

    SramBufferAllocator(const SramBuffer &) = delete;
    SramBufferAllocator& operator=(const SramBuffer &) = delete;
    SramBufferAllocator& operator=(SramBuffer &&) = delete;

    Expected<SramBuffer> allocate(size_t size)
    {
        size_t aligned_size = HailoRTCommon::align_to(size, SRAM_DMA_ALIGNMENT);
        if (m_in_use + aligned_size > SRAM_TOTAL_SIZE) {
            return make_unexpected(HAILO_OUT_OF_FW_MEMORY);
        }

        SramBuffer buffer(size, SRAM_BASE_DMA_ADDRESS + m_in_use);
        m_in_use += aligned_size;

        return buffer;
    }

private:
    size_t m_in_use = 0;

    constexpr static uint64_t SRAM_BASE_DMA_ADDRESS = 0x0000000060A00000;
    constexpr static size_t   SRAM_TOTAL_SIZE =       0x200000; // 2 Mega-Bytes
    constexpr static size_t   SRAM_DMA_ALIGNMENT =    0x1000;   // Aligned to 4096 TODO: Check this alignment.
};

}; /* namespace vdma */
}; /* namespace hailort */

#endif /* _HAILO_VDMA_SRAM_BUFFER_HPP_ */
