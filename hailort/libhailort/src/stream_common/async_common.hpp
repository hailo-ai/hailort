/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file async_common.hpp
 * @brief Common types/functions for async api
 **/

#ifndef _HAILO_ASYNC_COMMON_HPP_
#define _HAILO_ASYNC_COMMON_HPP_

#include "hailo/stream.hpp"

namespace hailort
{

// Internal function, wrapper to the user callbacks, accepts the callback status as an argument.
using InternalTransferDoneCallback = std::function<void(hailo_status)>;

struct TransferRequest {
    MemoryView buffer;
    InternalTransferDoneCallback callback;

    // Optional pre-mapped user buffer. If set, mapped_buffer must be the same as the "buffer"
    BufferPtr mapped_buffer = nullptr;
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_COMMON_HPP_ */
