/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file buffer_storage.cpp
 * @brief TODO: fill me (HRT-10026)
 **/

#include "buffer_storage.hpp"
#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/memory/dma_able_buffer.hpp"
#include "vdma/memory/mapped_buffer.hpp"
#include "common/utils.hpp"

namespace hailort
{

// Checking ABI of hailo_dma_buffer_direction_t vs HailoRTDriver::DmaDirection
static_assert(HAILO_DMA_BUFFER_DIRECTION_H2D == (int)HailoRTDriver::DmaDirection::H2D,
    "hailo_dma_buffer_direction_t must match HailoRTDriver::DmaDirection");
static_assert(HAILO_DMA_BUFFER_DIRECTION_D2H == (int)HailoRTDriver::DmaDirection::D2H,
    "hailo_dma_buffer_direction_t must match HailoRTDriver::DmaDirection");
static_assert(HAILO_DMA_BUFFER_DIRECTION_BOTH == (int)HailoRTDriver::DmaDirection::BOTH,
    "hailo_dma_buffer_direction_t must match HailoRTDriver::DmaDirection");


BufferStorageParams BufferStorageParams::create_dma()
{
    BufferStorageParams result{};
    result.flags = HAILO_BUFFER_FLAGS_DMA;
    return result;
}

BufferStorageParams::BufferStorageParams() :
    flags(HAILO_BUFFER_FLAGS_NONE)
{}

Expected<BufferStoragePtr> BufferStorage::create(size_t size, const BufferStorageParams &params)
{
    if (params.flags == HAILO_BUFFER_FLAGS_NONE) {
        auto result = HeapStorage::create(size);
        CHECK_EXPECTED(result);
        return std::static_pointer_cast<BufferStorage>(result.release());
    } else if (0 != (params.flags & HAILO_BUFFER_FLAGS_DMA)) {
        auto result = DmaStorage::create(size);
        CHECK_EXPECTED(result);
        return std::static_pointer_cast<BufferStorage>(result.release());
    } else if (0 != (params.flags & HAILO_BUFFER_FLAGS_CONTINUOUS)) {
        auto result = ContinuousStorage::create(size);
        CHECK_EXPECTED(result);
        return std::static_pointer_cast<BufferStorage>(result.release());
    }

    // TODO: HRT-10903
    LOGGER__ERROR("Buffer storage flags not currently supported {}", params.flags);
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<vdma::DmaAbleBufferPtr> BufferStorage::get_dma_able_buffer()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<uint64_t> BufferStorage::dma_address()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<HeapStoragePtr> HeapStorage::create(size_t size)
{
    std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[size]);
    CHECK_NOT_NULL_AS_EXPECTED(data, HAILO_OUT_OF_HOST_MEMORY);

    auto result = make_shared_nothrow<HeapStorage>(std::move(data), size);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

HeapStorage::HeapStorage(std::unique_ptr<uint8_t[]> data, size_t size) :
    m_data(std::move(data)),
    m_size(size)
{}

HeapStorage::HeapStorage(HeapStorage&& other) noexcept :
    BufferStorage(std::move(other)),
    m_data(std::move(other.m_data)),
    m_size(std::exchange(other.m_size, 0))
{}

size_t HeapStorage::size() const
{
    return m_size;
}

void *HeapStorage::user_address()
{
    return m_data.get();
}

Expected<void *> HeapStorage::release() noexcept
{
    m_size = 0;
    return m_data.release();
}


Expected<DmaStoragePtr> DmaStorage::create(size_t size)
{
    // TODO: HRT-10283 support sharing low memory buffers for DART and similar systems.
    TRY(auto dma_able_buffer, vdma::DmaAbleBuffer::create_by_allocation(size));

    auto result = make_shared_nothrow<DmaStorage>(std::move(dma_able_buffer));
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

DmaStorage::DmaStorage(vdma::DmaAbleBufferPtr &&dma_able_buffer) :
    m_dma_able_buffer(std::move(dma_able_buffer))
{}

size_t DmaStorage::size() const
{
    return m_dma_able_buffer->size();
}

void *DmaStorage::user_address()
{
    return m_dma_able_buffer->user_address();
}

Expected<void *> DmaStorage::release() noexcept
{
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<vdma::DmaAbleBufferPtr> DmaStorage::get_dma_able_buffer()
{
    return vdma::DmaAbleBufferPtr{m_dma_able_buffer};
}

Expected<ContinuousStoragePtr> ContinuousStorage::create(size_t size)
{
    TRY(auto driver, HailoRTDriver::create_integrated_nnc());
    TRY(auto continuous_buffer, vdma::ContinuousBuffer::create(size, *driver.get()));

    auto result = make_shared_nothrow<ContinuousStorage>(std::move(driver), std::move(continuous_buffer));
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

ContinuousStorage::ContinuousStorage(std::unique_ptr<HailoRTDriver> driver, vdma::ContinuousBuffer &&continuous_buffer) :
    m_driver(std::move(driver)),
    m_continuous_buffer(std::move(continuous_buffer))
{}

ContinuousStorage::ContinuousStorage(ContinuousStorage&& other) noexcept :
    BufferStorage(std::move(other)),
    m_driver(std::move(other.m_driver)),
    m_continuous_buffer(std::move(other.m_continuous_buffer))
{}

size_t ContinuousStorage::size() const
{
    return m_continuous_buffer.size();
}

void *ContinuousStorage::user_address()
{
    return m_continuous_buffer.user_address();
}

Expected<uint64_t> ContinuousStorage::dma_address()
{
    return m_continuous_buffer.dma_address();
}

Expected<void *> ContinuousStorage::release() noexcept
{
    return make_unexpected(HAILO_INVALID_OPERATION);
}

} /* namespace hailort */
