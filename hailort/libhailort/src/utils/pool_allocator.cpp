/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pool_allocator.cpp
 * @brief Pool allocator
 **/

#include "pool_allocator.hpp"

namespace hailort
{

Expected<std::shared_ptr<PoolAllocator>> PoolAllocator::create_shared(size_t pool_size, size_t buffer_size, std::function<Expected<Buffer>(size_t)> allocate_func)
{
    TRY(auto buffer_pool, FastBufferPool::create_shared(buffer_size, pool_size, allocate_func));

    auto allocator = make_shared_nothrow<PoolAllocator>(buffer_pool);
    CHECK_NOT_NULL(allocator, HAILO_OUT_OF_HOST_MEMORY);
    return allocator;
}

PoolAllocator::PoolAllocator(FastBufferPoolPtr buffer_pool) : m_buffer_pool(buffer_pool) {}

Expected<BufferPtr> PoolAllocator::allocate()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    TRY(auto buffer, m_buffer_pool->acquire());

    auto pooled_buffer_storage = make_shared_nothrow<PooledBufferStorage>(buffer, m_buffer_pool);
    CHECK_NOT_NULL(pooled_buffer_storage, HAILO_OUT_OF_HOST_MEMORY);

    return Buffer::create_shared(pooled_buffer_storage);
}

} /* namespace hailort */