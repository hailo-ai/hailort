/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pool_allocator.hpp
 * @brief Pool allocator
 **/

#ifndef _HAILO_POOL_ALLOCATOR_HPP_
#define _HAILO_POOL_ALLOCATOR_HPP_

#include "buffer_storage.hpp"
#include "common/buffer_pool.hpp"

#include <memory>

namespace hailort
{

class PoolAllocator final
{
public:
    static Expected<std::shared_ptr<PoolAllocator>> create_shared(size_t pool_size, size_t buffer_size, AllocateFunc allocate_func);

    PoolAllocator(FastBufferPoolPtr buffer_pool);
    Expected<BufferPtr> allocate();

private:
    FastBufferPoolPtr m_buffer_pool;
    std::mutex m_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_POOL_ALLOCATOR_HPP_ */