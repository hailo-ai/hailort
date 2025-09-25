/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_session_internal.hpp
 * @brief Hailo Session Header for sockets based comunication
 **/

#ifndef _POSIX_RAW_CONNECTION_INTERNAL_HPP_
#define _POSIX_RAW_CONNECTION_INTERNAL_HPP_

#include "hailo/expected.hpp"
#include "hailo/hailo_session.hpp"
#include "common/thread_safe_queue.hpp"
#include "common/socket.hpp"
#include "hrpc/connection_context.hpp"

#include <memory>

namespace hailort
{

struct AsyncAction
{
    std::function<hailo_status(bool)> action;
    std::function<void(hailo_status)> on_finish_callback;
};

class AsyncActionsThread
{
public:
    static Expected<std::shared_ptr<AsyncActionsThread>> create(size_t queue_size);
    AsyncActionsThread(SpscQueue<AsyncAction> &&queue, EventPtr shutdown_event);
    virtual ~AsyncActionsThread();

    hailo_status wait_for_enqueue_ready(std::chrono::milliseconds timeout);
    hailo_status enqueue_nonblocking(AsyncAction action);
    hailo_status abort();

private:
    hailo_status thread_loop();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    SpscQueue<AsyncAction> m_queue;
    EventPtr m_shutdown_event;
    std::thread m_thread;
};

class OsConnectionContext : public ConnectionContext
{
public:
    static Expected<std::shared_ptr<ConnectionContext>> create_shared(bool is_accepting);

    OsConnectionContext(bool is_accepting) : ConnectionContext(is_accepting) {}

    virtual ~OsConnectionContext() = default;
};

class OsListener : public SessionListener
{
public:
    static Expected<std::shared_ptr<SessionListener>> create_shared(std::shared_ptr<OsConnectionContext> context, uint16_t port);

    virtual ~OsListener() = default;

    virtual Expected<std::shared_ptr<Session>> accept() override;

    OsListener(Socket &&socket, std::shared_ptr<OsConnectionContext> context,
        std::shared_ptr<AsyncActionsThread> write_actions_thread,
        std::shared_ptr<AsyncActionsThread> read_actions_thread, uint16_t port) :
            SessionListener(port), m_socket(std::move(socket)),
            m_context(context), m_write_actions_thread(write_actions_thread),
            m_read_actions_thread(read_actions_thread) {}

private:
    static Expected<std::shared_ptr<OsListener>> create_by_addr_server(std::shared_ptr<OsConnectionContext> context,
        const std::string &ip, uint16_t port);
    static Expected<std::shared_ptr<OsListener>> create_localhost_server(std::shared_ptr<OsConnectionContext> context, uint16_t port);
    
    Socket m_socket;
    std::shared_ptr<OsConnectionContext> m_context;
    std::shared_ptr<AsyncActionsThread> m_write_actions_thread;
    std::shared_ptr<AsyncActionsThread> m_read_actions_thread;

};

class OsSession : public Session
{
public:
    static Expected<std::shared_ptr<OsSession>> connect(std::shared_ptr<OsConnectionContext> context, uint16_t port);

    virtual ~OsSession();

    virtual hailo_status write(const uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_WRITE_TIMEOUT) override;
    virtual hailo_status read(uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT) override;
    virtual hailo_status close() override;

    virtual hailo_status wait_for_write_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    using Session::write_async;
    virtual hailo_status write_async(TransferRequest &&request) override;

    virtual hailo_status wait_for_read_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    using Session::read_async;
    virtual hailo_status read_async(TransferRequest &&request) override;

    virtual Expected<Buffer> allocate_buffer(size_t size, hailo_dma_buffer_direction_t direction) override;

    OsSession(Socket &&socket, std::shared_ptr<OsConnectionContext> context,
        std::shared_ptr<AsyncActionsThread> write_actions_thread,
        std::shared_ptr<AsyncActionsThread> read_actions_thread, uint16_t port) :
            Session(port), m_socket(std::move(socket)), m_context(context),
            m_write_actions_thread(write_actions_thread),
            m_read_actions_thread(read_actions_thread) {}

    static Expected<sockaddr_un> get_localhost_server_addr();
    hailo_status connect();

private:
    static Expected<std::shared_ptr<OsSession>> create_by_addr_client(std::shared_ptr<OsConnectionContext> context,
        const std::string &ip, uint16_t port);
    static Expected<std::shared_ptr<OsSession>> create_localhost_client(std::shared_ptr<OsConnectionContext> context, uint16_t port);

    hailo_status connect_by_addr(const std::string &ip, uint16_t port);
    hailo_status connect_localhost();

    std::mutex m_read_mutex;
    std::condition_variable m_read_cv;
    std::mutex m_write_mutex;
    std::condition_variable m_write_cv;
    Socket m_socket;
    std::shared_ptr<OsConnectionContext> m_context;
    std::shared_ptr<AsyncActionsThread> m_write_actions_thread;
    std::shared_ptr<AsyncActionsThread> m_read_actions_thread;
};

} // namespace hailort

#endif // _POSIX_RAW_CONNECTION_INTERNAL_HPP_