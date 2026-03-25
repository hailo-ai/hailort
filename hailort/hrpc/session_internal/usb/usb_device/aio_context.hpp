/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file aio_context.hpp
 * @brief AIO Context Implementation
 **/

#ifndef _AIO_CONTEXT_HPP_
#define _AIO_CONTEXT_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include <libaio.h>
#include <memory>
#include <atomic>

namespace hailort
{

class AioContext final
{
public:
    static Expected<std::unique_ptr<AioContext>> create(size_t max_ongoing_transfers);
    ~AioContext();

    AioContext(const AioContext &other) = delete;
    AioContext &operator=(const AioContext &other) = delete;
    AioContext(AioContext &&other) = delete;
    AioContext &operator=(AioContext &&other) = delete;

    hailo_status submit_read(int fd, uint8_t *data, size_t size, size_t offset);
    hailo_status submit_write(int fd, const uint8_t *data, size_t size, size_t offset);
    Expected<size_t> wait_for_completion(int fd, const std::atomic_bool &is_closed);
    
    // Signals the eventfd to unblock a pending wait_for_completion().
    void wake();

    AioContext(io_context_t ctx, int eventfd);

private:
    io_context_t m_ctx;
    int m_eventfd;
    struct iocb m_iocb;
};

} // namespace hailort

#endif // _AIO_CONTEXT_HPP_
