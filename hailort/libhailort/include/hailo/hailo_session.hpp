/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_session.hpp
 * @brief Includes the classes for the basic functionality of connecting, reading and writing over the connection.
 **/

#ifndef _HAILO_SESSION_HPP_
#define _HAILO_SESSION_HPP_

#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include <memory>
#include <chrono>
#include <functional>
#include <string>

#define BACKLOG_SIZE (16)

namespace hailort
{

class ConnectionContext;
class Session;
struct TransferRequest;

/**
 * The Listener class is used to accept new connections.
*/
class HAILORTAPI SessionListener
{
public:
    SessionListener() = default;
    virtual ~SessionListener() = default;

    /**
     * Creates a new SessionListener.
     * This function should be used from the server.
     * The returned SessionListener object should be used to accept new clients.
     *
     * @param[in] port                  The port to listen on.
     * @param[in] ip                    The IP address to listen on.
     * @return Upon success, returns Expected of a shared pointer of listener, representing the listener object.
    */
    static Expected<std::shared_ptr<SessionListener>> create_shared(uint16_t port, const std::string &ip = "");

    /**
     * This function should be called by the server side (device) in order to accept a new connection.
     * This call is blocking and will wait until a new client connection is established.
     *
     * @return Upon success, returns Expected of a shared pointer of a Session, representing the connection with
     * the new client.
     */
    virtual hailort::Expected<std::shared_ptr<Session>> accept() = 0;

protected:
    explicit SessionListener(uint16_t port) : m_port(port) {}
    uint16_t m_port;

private:
    static Expected<std::shared_ptr<SessionListener>> create_shared(std::shared_ptr<ConnectionContext> context, uint16_t port);

    friend class Server;
    friend class RawConnectionWrapper;
};

/**
 * The Session class provides the basic functionality for connecting, reading and writing (synchonously and asynchronously)
 * over the connection.
*/
class HAILORTAPI Session
{
public:
    Session() = default;
    virtual ~Session() = default;

    /**
     * Creates a new Session and connects to the server.
     * This function should be used from the client side.
     *
     * @param[in] port  The port to connect to.
     * @param[in] device_id  The device id to connect to.
     * @return Upon success, returns Expected of a shared pointer of session, representing the session object.
     */
    static Expected<std::shared_ptr<Session>> connect(uint16_t port, const std::string &device_id = "");

    /**
     * Writes the entire buffer over the connection, synchronously.
     *
     * @param[in] buffer    The buffer to be written.
     * @param[in] size      The size of the buffer given.
     * @param[in] timeout   The timeout for the write operation.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     *
     * @note The write pattern should match the read pattern. For example, if 10 bytes are written followed
     *       by 8 bytes, they should be read in the same order: 10 bytes first, then 8 bytes.
     */
    virtual hailo_status write(const uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_WRITE_TIMEOUT) = 0;

    /**
     * Reads the entire buffer over the connection, synchronously.
     *
     * @param[in] buffer   A pointer to a buffer that receives the data read from the connection.
     * @param[in] size     The size of the given buffer.
     * @param[in] timeout  The timeout for the read operation.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     *
     * @note The read pattern should match the write pattern. For example, if 10 bytes are written followed
     *       by 8 bytes, they should be read in the same order: 10 bytes first, then 8 bytes.
     */
    virtual hailo_status read(uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT) = 0;

    /**
     * Closes the connection.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     */
    virtual hailo_status close() = 0;

    /**
     * Waits until the session is ready to launch a new call to `Session::write_async()`. Each session has a
     * limited-size queue for ongoing transfers.
     *
     * @param[in] transfer_size     The size of buffer to be written.
     * @param[in] timeout           Amount of time to wait until the session is ready in milliseconds.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If @a timeout_ms has passed and the session is not ready, returns ::HAILO_TIMEOUT.
     *           - In any other error case, returns ::hailo_status error.
     */
    virtual hailo_status wait_for_write_async_ready(size_t transfer_size,
        std::chrono::milliseconds timeout) = 0;

    /**
     * Writes the contents of @a buffer over the connection asynchronously, initiating a deferred operation that will be
     * completed later.
     * - @a callback is triggered upon successful completion or failure of the deferred operation. The callback
     *   receives a ::hailo_status object containing the transfer status.
     *
     * @param[in] buffer            The buffer to be written.
     * @param[in] size              The size of the given buffer.
     * @param[in] callback          The callback that will be called when the transfer is complete or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise returns a ::hailo_status error.
     *
     * @note Until @a callback is called, the user cannot change or delete @a buffer.
     * @note The write pattern should match the read pattern. For example, if 10 bytes are written followed
     *       by 8 bytes, they should be read in the same order: 10 bytes first, then 8 bytes.
     */
    virtual hailo_status write_async(const uint8_t *buffer, size_t size,
        std::function<void(hailo_status)> &&callback);

    // Internal
    virtual hailo_status write_async(TransferRequest &&request) = 0;

    /**
     * Waits until the session is ready to launch a new call to `Session::read_async()`. Each session has a
     * limited-size queue for ongoing transfers.
     *
     * @param[in] transfer_size     The size of buffer to be read.
     * @param[in] timeout           Amount of time to wait until the session is ready in milliseconds.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If @a timeout_ms has passed and the session is not ready, returns ::HAILO_TIMEOUT.
     *           - In any other error case, returns ::hailo_status error.
     */
    virtual hailo_status wait_for_read_async_ready(size_t transfer_size,
        std::chrono::milliseconds timeout) = 0;

    /**
     * Reads into @a buffer over the connection asynchronously, initiating a deferred operation that will be
     * completed later.
     * - @a callback is triggered upon successful completion or failure of the deferred operation. The callback
     *   receives a ::hailo_status object containing the transfer status.
     *
     * @param[in] buffer            The buffer to be read into.
     * @param[in] size              The size of the given buffer.
     * @param[in] callback          The callback that will be called when the transfer is complete or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise returns a ::hailo_status error.
     *
     * @note Until @a callback is called, the user cannot change or delete @a buffer.
     * @note The read pattern should match the write pattern. For example, if 10 bytes are written followed
     *       by 8 bytes, they should be read in the same order: 10 bytes first, then 8 bytes.
     */
    virtual hailo_status read_async(uint8_t *buffer, size_t size,
        std::function<void(hailo_status)> &&callback);

    // Internal
    virtual hailo_status read_async(TransferRequest &&request) = 0;
    virtual Expected<int> read_fd() = 0;

    virtual Expected<Buffer> allocate_buffer(size_t size, hailo_dma_buffer_direction_t direction) = 0;

    static constexpr std::chrono::milliseconds DEFAULT_WRITE_TIMEOUT = std::chrono::milliseconds(10000);
    static constexpr std::chrono::milliseconds DEFAULT_READ_TIMEOUT = std::chrono::milliseconds(HAILO_INFINITE);
protected:
    explicit Session(uint16_t port) : m_port(port) {}
    uint16_t m_port;

private:
    static Expected<std::shared_ptr<Session>> connect(std::shared_ptr<ConnectionContext> context, uint16_t port);

    friend class Client;
    friend class RawConnectionWrapper;
};

} // namespace hailort

#endif // _HAILO_SESSION_HPP_