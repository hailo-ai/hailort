/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_session_internal.cpp
 * @brief Linux Sockets Hailo Session
 **/

#include "hrpc/raw_connection_internal/socket/hailo_session_internal.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/internal_env_vars.hpp"
#include "common/filesystem.hpp"
#include "vdma/channel/transfer_common.hpp"

#include <string>

#define USE_DEFAULT_PROTOCOL (0)
#define HRT_UNIX_SOCKET_FILE_NAME ("hailort_unix_socket")

namespace hailort
{

// Same as in pcie_session.cpp
static constexpr uint64_t MAX_ONGOING_TRANSFERS = 128;

Expected<std::shared_ptr<AsyncActionsThread>> AsyncActionsThread::create(size_t queue_size)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto write_queue, SpscQueue<AsyncAction>::create(queue_size, shutdown_event));

    auto ptr = make_shared_nothrow<AsyncActionsThread>(std::move(write_queue), shutdown_event);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

AsyncActionsThread::AsyncActionsThread(SpscQueue<AsyncAction> &&queue, EventPtr shutdown_event) :
    m_queue(std::move(queue)), m_shutdown_event(shutdown_event), m_current_queue_size(0)
{
    m_thread = std::thread([this] () { thread_loop(); });
}

hailo_status AsyncActionsThread::abort()
{
    auto status = m_shutdown_event->signal();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to signal shutdown event, status = {}", status);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    const bool IGNORE_SHUTDOWN_EVENT = true;
    while (true) {
        auto action = m_queue.dequeue(std::chrono::milliseconds(0), IGNORE_SHUTDOWN_EVENT);
        if (HAILO_TIMEOUT == action.status()) {
            break;
        }
        if (!action) {
            status = action.status();
            LOGGER__ERROR("Failed to dequeue action, status = {}", status);
            continue;
        }

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_current_queue_size--;
        }
        m_cv.notify_one();

        action->on_finish_callback(action->action(true));
    }
    return status;
}

AsyncActionsThread::~AsyncActionsThread()
{
    abort();
}

hailo_status AsyncActionsThread::thread_loop()
{
    while (true) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto action,
            m_queue.dequeue(std::chrono::milliseconds(HAILO_INFINITE)));
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_current_queue_size--;
        }
        m_cv.notify_one();
        action.on_finish_callback(action.action(false));
    }
    return HAILO_SUCCESS;
}

hailo_status AsyncActionsThread::wait_for_enqueue_ready(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    CHECK(m_cv.wait_for(lock, timeout, [this] () {
        return m_current_queue_size < m_queue.max_capacity();
    }), HAILO_TIMEOUT, "Timeout waiting for enqueue ready");
    return HAILO_SUCCESS;
}

hailo_status AsyncActionsThread::enqueue_nonblocking(AsyncAction action)
{
    auto status = m_queue.enqueue(action, std::chrono::milliseconds(0));
    CHECK(status != HAILO_TIMEOUT, HAILO_QUEUE_IS_FULL, "Queue is full, queue size = {}",
        m_queue.size_approx());// Should call wait_for_enqueue_ready() before enqueue_nonblocking()
    CHECK_SUCCESS(status);

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_current_queue_size++;
    }
    m_cv.notify_one();

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<ConnectionContext>> OsConnectionContext::create_client_shared(const std::string &ip)
{
    auto ptr = make_shared_nothrow<OsConnectionContext>(false, ip);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

Expected<std::shared_ptr<ConnectionContext>> OsConnectionContext::create_server_shared(const std::string &ip)
{
    auto ptr = make_shared_nothrow<OsConnectionContext>(true, ip);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::dynamic_pointer_cast<ConnectionContext>(ptr);
}

static Expected<Socket> create_inet_socket()
{
    TRY(auto socket, Socket::create(AF_INET, SOCK_STREAM, USE_DEFAULT_PROTOCOL));

    CHECK_SUCCESS(socket.allow_reuse_address());

    auto interface_name = get_env_variable(HAILO_SOCKET_BIND_TO_INTERFACE_ENV_VAR);
    if (interface_name) {
        CHECK_SUCCESS(socket.bind_to_device(interface_name.value()));
    }

    return socket;
}

Expected<std::shared_ptr<Session>> OsListener::accept()
{
    TRY(auto client_socket, m_socket.accept());

    TRY(auto write_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    TRY(auto read_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));

    std::shared_ptr<OsSession> ptr = make_shared_nothrow<OsSession>(std::move(client_socket), m_context,
        write_actions_thread, read_actions_thread, m_port);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::dynamic_pointer_cast<Session>(ptr);
}

Expected<std::shared_ptr<SessionListener>> OsListener::create_shared(std::shared_ptr<OsConnectionContext> context, uint16_t port)
{
    std::shared_ptr<SessionListener> ptr;
    auto ip_addr = context->get_ip();
    if (SERVER_ADDR_USE_UNIX_SOCKET == ip_addr) {
        TRY(ptr, create_localhost_server(context, port));
    } else {
        TRY(ptr, create_by_addr_server(context, ip_addr, port));
    }

    return ptr;
}

Expected<std::shared_ptr<OsListener>> OsListener::create_by_addr_server(std::shared_ptr<OsConnectionContext> context,
    const std::string &ip, uint16_t port)
{
    TRY(auto socket, create_inet_socket());

    sockaddr_in server_addr = {};
    socklen_t addr_len = sizeof(server_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    auto status = socket.pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    CHECK_SUCCESS_AS_EXPECTED(status,
        "Failed to run 'inet_pton'. make sure the provided IP address is valid");

    status = socket.socket_bind((struct sockaddr*)&server_addr, addr_len);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = socket.listen(BACKLOG_SIZE);
    CHECK_SUCCESS_AS_EXPECTED(status);

    TRY(auto write_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    TRY(auto read_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));

    auto res = make_shared_nothrow<OsListener>(std::move(socket), context, write_actions_thread, read_actions_thread, port);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

Expected<std::shared_ptr<OsListener>> OsListener::create_localhost_server(std::shared_ptr<OsConnectionContext> context, uint16_t port)
{
    TRY(auto socket, Socket::create(AF_UNIX, SOCK_STREAM, USE_DEFAULT_PROTOCOL));

    TRY(sockaddr_un server_addr, OsSession::get_localhost_server_addr(port));
    std::remove(server_addr.sun_path);

    auto status = socket.socket_bind((struct sockaddr*)&server_addr, sizeof(server_addr));
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = socket.listen(BACKLOG_SIZE);
    CHECK_SUCCESS_AS_EXPECTED(status);

    TRY(auto write_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    TRY(auto read_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));

    auto ptr = make_shared_nothrow<OsListener>(std::move(socket), context, write_actions_thread, read_actions_thread, port);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

OsSession::~OsSession()
{
    close();
}

Expected<std::shared_ptr<OsSession>> OsSession::connect(std::shared_ptr<OsConnectionContext> context, uint16_t port)
{
    (void)port;
    // Creates one of the following 2 types of sessions:
    // Unix Socket client - for local communication
    // TCP client - for remote communication - using ip and port
    std::shared_ptr<OsSession> ptr;
    auto ip_addr = context->get_ip();
    if (SERVER_ADDR_USE_UNIX_SOCKET == ip_addr) {
        TRY(ptr, create_localhost_client(context, port));
    } else {
        TRY(ptr, create_by_addr_client(context, ip_addr, port));
    }
    auto status = ptr->connect();
    CHECK_SUCCESS(status);
    return ptr;
}

Expected<std::shared_ptr<OsSession>> OsSession::create_localhost_client(std::shared_ptr<OsConnectionContext> context, uint16_t port)
{
    TRY(auto socket, Socket::create(AF_UNIX, SOCK_STREAM, USE_DEFAULT_PROTOCOL));

    TRY(auto write_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    TRY(auto read_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    
    auto ptr = make_shared_nothrow<OsSession>(std::move(socket), context, write_actions_thread, read_actions_thread, port);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

Expected<std::shared_ptr<OsSession>> OsSession::create_by_addr_client(std::shared_ptr<OsConnectionContext> context,
    const std::string &ip, uint16_t port)
{
    TRY(auto socket, create_inet_socket());

    sockaddr_in server_addr = {};
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    auto status = socket.pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    CHECK_SUCCESS_AS_EXPECTED(status,
        "Failed to run 'inet_pton'. make sure the ip address is set correctly <ip>");

    TRY(auto write_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    TRY(auto read_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));

    auto res = make_shared_nothrow<OsSession>(std::move(socket), context, write_actions_thread, read_actions_thread, port);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

hailo_status OsSession::connect()
{
    auto ip_addr = m_context->get_ip();
    if (SERVER_ADDR_USE_UNIX_SOCKET == ip_addr) {
        return connect_localhost(m_port);
    } else {
        return connect_by_addr(ip_addr, m_port);
    }
}

hailo_status OsSession::connect_localhost(uint16_t port)
{
    TRY(sockaddr_un server_addr, get_localhost_server_addr(port));
    auto status = m_socket.connect((struct sockaddr*)&server_addr, sizeof(server_addr));
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<sockaddr_un> OsSession::get_localhost_server_addr(uint16_t port)
{
    TRY(auto tmp_path, Filesystem::get_temp_path());
    std::string addr = tmp_path + HRT_UNIX_SOCKET_FILE_NAME + "_" + std::to_string(port);

    struct sockaddr_un server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, addr.c_str(), addr.size());

    return server_addr;
}

hailo_status OsSession::connect_by_addr(const std::string &ip, uint16_t port)
{
    sockaddr_in server_addr = {};
    socklen_t addr_len = sizeof(server_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    auto status = m_socket.pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    CHECK_SUCCESS_AS_EXPECTED(status,
        "Failed to run 'inet_pton'. make sure 'HAILO_SOCKET_COM_ADDR_XX' is set correctly <ip>");
    status = m_socket.connect((struct sockaddr*)&server_addr, addr_len);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status OsSession::write(const uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    auto status = wait_for_write_async_ready(size, timeout);
    CHECK_SUCCESS(status);

    status = write_async(buffer, size, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(m_write_mutex);
            assert(status != HAILO_UNINITIALIZED);
            if (HAILO_STREAM_ABORT == status) {
                transfer_status = HAILO_COMMUNICATION_CLOSED;
            } else {
                transfer_status = status;
            }
        }
        m_write_cv.notify_one();
    });
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_write_mutex);
    CHECK(m_write_cv.wait_for(lock, timeout, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

hailo_status OsSession::read(uint8_t *buffer, size_t size, std::chrono::milliseconds timeout)
{
    hailo_status transfer_status = HAILO_UNINITIALIZED;

    auto status = wait_for_read_async_ready(size, timeout);
    CHECK_SUCCESS(status);

    status = read_async(buffer, size, [&] (hailo_status status) {
        {
            std::unique_lock<std::mutex> lock(m_read_mutex);
            assert(status != HAILO_UNINITIALIZED);
            if (HAILO_STREAM_ABORT == status) {
                transfer_status = HAILO_COMMUNICATION_CLOSED;
            } else {
                transfer_status = status;
            }
        }
        m_read_cv.notify_one();
    });
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(m_read_mutex);
    CHECK(m_read_cv.wait_for(lock, timeout, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");

    return transfer_status;
}

hailo_status OsSession::close()
{
    std::unique_lock<std::mutex> lock(m_close_mutex);

    if (INVALID_SOCKET == m_socket.get_fd()) { // In case of double close
        return HAILO_SUCCESS;
    }

    auto status = m_socket.abort();
    CHECK_SUCCESS(status);

    status = m_write_actions_thread->abort();
    CHECK_SUCCESS(status);

    status = m_read_actions_thread->abort();
    CHECK_SUCCESS(status);

    status = m_socket.close_socket_fd();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status OsSession::wait_for_write_async_ready(size_t /*transfer_size*/, std::chrono::milliseconds timeout)
{
    return m_write_actions_thread->wait_for_enqueue_ready(timeout);
}

hailo_status OsSession::write_async(TransferRequest &&request)
{
    return m_write_actions_thread->enqueue_nonblocking({[this, buffers=std::move(request.transfer_buffers)] (bool is_aborted) -> hailo_status {
        if (is_aborted) {
            return HAILO_COMMUNICATION_CLOSED;
        }

        for (auto transfer_buffer : buffers) {
            if (transfer_buffer.type() == TransferBufferType::DMABUF) {
                TRY(auto fd, transfer_buffer.dmabuf_fd());
                CHECK_SUCCESS(m_socket.write_fd(fd, transfer_buffer.size()));
            } else {
                TRY(auto buffer, transfer_buffer.base_buffer());
                auto status = m_socket.sendall(buffer.data(), buffer.size(), MSG_NOSIGNAL);
                if (HAILO_ETH_FAILURE == status) {
                    return HAILO_COMMUNICATION_CLOSED;
                }
                CHECK_SUCCESS(status);
            }
        }
        return HAILO_SUCCESS;
    }, request.callback});
}

hailo_status OsSession::wait_for_read_async_ready(size_t /*transfer_size*/, std::chrono::milliseconds timeout)
{
    return m_read_actions_thread->wait_for_enqueue_ready(timeout);
}

hailo_status OsSession::read_async(TransferRequest &&request)
{
    return m_read_actions_thread->enqueue_nonblocking({[this, buffers=std::move(request.transfer_buffers)] (bool is_aborted) -> hailo_status {
        if (is_aborted) {
            return HAILO_COMMUNICATION_CLOSED;
        }

        for (auto transfer_buffer : buffers) {
            TRY(auto buffer, transfer_buffer.base_buffer());
            auto status = m_socket.recvall(buffer.data(), buffer.size());
            if (HAILO_COMMUNICATION_CLOSED == status) {
                return status;
            }
            if (HAILO_ETH_FAILURE == status) {
                return HAILO_COMMUNICATION_CLOSED;
            }
            CHECK_SUCCESS(status);
        }

        return HAILO_SUCCESS;
    }, request.callback});
}

Expected<int> OsSession::read_fd()
{
    return m_socket.read_fd();
}

Expected<Buffer> OsSession::allocate_buffer(size_t size, hailo_dma_buffer_direction_t)
{
    return Buffer::create(size);
}

} // namespace hailort