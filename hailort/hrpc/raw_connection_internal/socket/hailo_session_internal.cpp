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

#define BACKLOG_SIZE (5)
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

AsyncActionsThread::AsyncActionsThread(SpscQueue<AsyncAction> &&queue,
    EventPtr shutdown_event) : m_queue(std::move(queue)), m_shutdown_event(shutdown_event)
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
        action->on_finish_callback(action->action(true));
    }

    m_cv.notify_all();
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
        m_cv.notify_one();
        action.on_finish_callback(action.action(false));
    }
    return HAILO_SUCCESS;
}

hailo_status AsyncActionsThread::wait_for_enqueue_ready(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    CHECK(m_cv.wait_for(lock, timeout, [this] () {
        return !m_queue.is_queue_full();
    }), HAILO_TIMEOUT, "Timeout waiting for enqueue ready");
    return HAILO_SUCCESS;
}

hailo_status AsyncActionsThread::enqueue_nonblocking(AsyncAction action)
{
    auto status = m_queue.enqueue(action, std::chrono::milliseconds(0));
    CHECK(status != HAILO_TIMEOUT, HAILO_QUEUE_IS_FULL); // Should call wait_for_enqueue_ready() before enqueue_nonblocking()
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<ConnectionContext>> OsConnectionContext::create_shared(bool is_accepting)
{
    auto ptr = make_shared_nothrow<OsConnectionContext>(is_accepting);
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
    auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR);
    CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
    if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
        TRY(ptr, create_localhost_server(context, port));
    } else {
        auto ip = force_socket_com_value.value();
        TRY(ptr, create_by_addr_server(context, ip, port));
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
        "Failed to run 'inet_pton'. make sure 'HAILO_SOCKET_COM_ADDR_SERVER' is set correctly <ip>)");

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

    TRY(sockaddr_un server_addr, OsSession::get_localhost_server_addr());
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
    auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR);
    CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
    if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
        TRY(ptr, create_localhost_client(context, port));
    } else {
        auto ip = force_socket_com_value.value();
        TRY(ptr, create_by_addr_client(context, ip, port));
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
        "Failed to run 'inet_pton'. make sure 'HAILO_SOCKET_COM_ADDR_CLIENT' is set correctly <ip>");

    TRY(auto write_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));
    TRY(auto read_actions_thread, AsyncActionsThread::create(MAX_ONGOING_TRANSFERS));

    auto res = make_shared_nothrow<OsSession>(std::move(socket), context, write_actions_thread, read_actions_thread, port);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);
    return res;
}

hailo_status OsSession::connect()
{
    if (m_context->is_accepting()) {
        auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR);
        CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
        if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
            return connect_localhost();
        } else {
            auto ip = force_socket_com_value.value();
            return connect_by_addr(ip, m_port);
        }
    } else {
        auto force_socket_com_value = get_env_variable(HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR);
        CHECK_EXPECTED(force_socket_com_value); // We know its set, otherwise we'll be working with PCIeRawCon
        if (HAILO_SOCKET_COM_ADDR_UNIX_SOCKET == force_socket_com_value.value()) {
            return connect_localhost();
        } else {
            auto ip = force_socket_com_value.value();
            return connect_by_addr(ip, m_port);
        }
    }
}

hailo_status OsSession::connect_localhost()
{
    TRY(sockaddr_un server_addr, get_localhost_server_addr());
    auto status = m_socket.connect((struct sockaddr*)&server_addr, sizeof(server_addr));
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<sockaddr_un> OsSession::get_localhost_server_addr()
{
    TRY(auto tmp_path, Filesystem::get_temp_path());
    std::string addr = tmp_path + HRT_UNIX_SOCKET_FILE_NAME;

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
            transfer_status = status;
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
            transfer_status = status;
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
            return HAILO_STREAM_ABORT;
        }

        for (auto transfer_buffer : buffers) {
            TRY(auto buffer, transfer_buffer.base_buffer());
            auto status = m_socket.sendall(buffer.data(), buffer.size(), MSG_NOSIGNAL);
            CHECK_SUCCESS(status);
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
            return HAILO_STREAM_ABORT;
        }

        for (auto transfer_buffer : buffers) {
            TRY(auto buffer, transfer_buffer.base_buffer());
            auto status = m_socket.recvall(buffer.data(), buffer.size());
            if (HAILO_COMMUNICATION_CLOSED == status) {
                return status;
            }
            CHECK_SUCCESS(status);
        }

        return HAILO_SUCCESS;
    }, request.callback});
}

Expected<Buffer> OsSession::allocate_buffer(size_t size, hailo_dma_buffer_direction_t)
{
    return Buffer::create(size);
}

} // namespace hailort