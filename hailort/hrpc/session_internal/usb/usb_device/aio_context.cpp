/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file aio_context.cpp
 * @brief AIO Context Implementation
 **/

#include "hrpc/session_internal/usb/usb_device/aio_context.hpp"
#include "common/utils.hpp"
#include "common/logger_macros.hpp"

#include <sys/eventfd.h>
#include <sys/select.h>
#include <unistd.h>

namespace hailort
{

static constexpr int AIO_SELECT_TIMEOUT_SECONDS {10};
static constexpr int AIO_ENABLE_NOTIFICATIONS {1 << 0};

Expected<std::unique_ptr<AioContext>> AioContext::create(size_t max_ongoing_transfers)
{
    io_context_t ctx = nullptr;
    const int ret = io_setup(static_cast<int>(max_ongoing_transfers), &ctx);
    CHECK(0 == ret, HAILO_INTERNAL_FAILURE, "Failed to setup AIO context: {}", strerror(errno));
    auto aio_cleanup_guard = defer([&ctx]() { (void)io_destroy(ctx); });

    const int eventfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(-1 != eventfd, HAILO_INTERNAL_FAILURE, "Failed to create eventfd: {}", strerror(errno));
    auto eventfd_cleanup_guard = defer([eventfd]() { (void)::close(eventfd); });

    auto aio_context = make_unique_nothrow<AioContext>(ctx, eventfd);
    CHECK_NOT_NULL(aio_context, HAILO_OUT_OF_HOST_MEMORY);

    aio_cleanup_guard.release();
    eventfd_cleanup_guard.release();
    return aio_context;
}

AioContext::AioContext(io_context_t ctx, int eventfd)
    : m_ctx(ctx), m_eventfd(eventfd), m_iocb({})
{}

AioContext::~AioContext()
{
    int ret = 0;
    if (m_ctx != nullptr) {
        ret = io_destroy(m_ctx);
        if (ret < 0) {
            LOGGER__ERROR("Failed to destroy AIO context: {}", strerror(-ret));
        }
        m_ctx = nullptr;
    }
    if (m_eventfd >= 0) {
        ret = ::close(m_eventfd);
        if (ret < 0) {
            LOGGER__ERROR("Failed to close AIO eventfd: {}", strerror(errno));
        }
        m_eventfd = -1;
    }
}

//Submits an asynchronous read from the beginning of the file into a buffer and notifies completion via an eventfd.
hailo_status AioContext::submit_read(int fd, uint8_t *data, size_t size, size_t offset)
{
    io_prep_pread(&m_iocb, fd, data + offset, size - offset, 0);
    
    m_iocb.u.c.flags |= AIO_ENABLE_NOTIFICATIONS;
    m_iocb.u.c.resfd = m_eventfd;

    struct iocb *iocb_array[] = {&m_iocb};

    const int ret = io_submit(m_ctx, 1, iocb_array);
    CHECK(1 == ret, HAILO_INTERNAL_FAILURE, "io_submit failed: {}", strerror(-ret));
    return HAILO_SUCCESS;
}

//Submits an asynchronous write from a buffer into the beginning of a file and notifies completion via an eventfd.
hailo_status AioContext::submit_write(int fd, const uint8_t *data, size_t size, size_t offset)
{
    io_prep_pwrite(&m_iocb, fd, const_cast<uint8_t*>(data) + offset, size - offset, 0);
    
    m_iocb.u.c.flags |= AIO_ENABLE_NOTIFICATIONS;
    m_iocb.u.c.resfd = m_eventfd;

    struct iocb *iocb_array[] = {&m_iocb};

    const int ret = io_submit(m_ctx, 1, iocb_array);
    CHECK(1 == ret, HAILO_INTERNAL_FAILURE, "io_submit failed: {}", strerror(-ret));
    return HAILO_SUCCESS;
}

Expected<size_t> AioContext::wait_for_completion(int fd, const std::atomic_bool &is_closed)
{
    fd_set read_fds;
    struct timeval timeout;
    
    while (true) {
        if (is_closed.load()) {
            return make_unexpected(HAILO_COMMUNICATION_CLOSED);
        }

        FD_ZERO(&read_fds);
        FD_SET(m_eventfd, &read_fds);
        FD_SET(fd, &read_fds);
        
        const int max_fd = std::max(m_eventfd, fd);
        timeout.tv_sec = AIO_SELECT_TIMEOUT_SECONDS;
        timeout.tv_usec = 0;

        int ret = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        CHECK(0 != ret, make_unexpected(HAILO_TIMEOUT), "Timeout");

        if (!FD_ISSET(m_eventfd, &read_fds)) {
            continue;
        }

        uint64_t finished_count = 0;
        ssize_t ev_read = ::read(m_eventfd, &finished_count, sizeof(finished_count));
        CHECK(ev_read >= 0, HAILO_INTERNAL_FAILURE, "Failed to read from eventfd: {}", strerror(errno));

        if (is_closed.load()) {
            return make_unexpected(HAILO_COMMUNICATION_CLOSED);
        }

        struct io_event event = {};
        ret = io_getevents(m_ctx, 1, 1, &event, nullptr);
        CHECK(ret > 0, HAILO_INTERNAL_FAILURE, "io_getevents failed: {}", strerror(-ret));
        
        const ssize_t bytes_read = event.res;
        if (bytes_read <= 0) {
            return make_unexpected(HAILO_COMMUNICATION_CLOSED);
        }
        return static_cast<size_t>(bytes_read);
    }
}

void AioContext::wake()
{
    uint64_t value = 1;
    const auto written = ::write(m_eventfd, &value, sizeof(value));
    if ((written < 0) && (EAGAIN != errno)) {
        LOGGER__ERROR("Failed to wake AIO context: {}", strerror(errno));
    }
}

} // namespace hailort
