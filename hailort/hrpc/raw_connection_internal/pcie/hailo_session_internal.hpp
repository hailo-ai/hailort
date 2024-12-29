/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file hailo_session_internal.hpp
 * @brief Hailo Session Header for pcie based comunication
 **/

#ifndef _PCIE_RAW_CONNECTION_INTERNAL_HPP_
#define _PCIE_RAW_CONNECTION_INTERNAL_HPP_

#include "hailo/expected.hpp"
#include "vdma/pcie_session.hpp"
#include "hailo/hailo_session.hpp"
#include "hrpc/connection_context.hpp"

#include <memory>
#include <condition_variable>

namespace hailort
{

class PcieConnectionContext : public ConnectionContext
{
public:
    static Expected<std::shared_ptr<ConnectionContext>> create_client_shared(const std::string &device_id);
    static Expected<std::shared_ptr<ConnectionContext>> create_server_shared();

    PcieConnectionContext(std::shared_ptr<HailoRTDriver> &&driver, bool is_accepting)
        : ConnectionContext(is_accepting), m_driver(std::move(driver)), m_conn_count(0) {}

    virtual ~PcieConnectionContext() = default;

    virtual std::shared_ptr<HailoRTDriver> get_driver() override { return m_driver; }

    hailo_status wait_for_available_connection();
    void mark_connection_closed();

private:
    std::shared_ptr<HailoRTDriver> m_driver;
    uint32_t m_conn_count;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

class RawPcieSession : public Session
{
public:
    static Expected<std::shared_ptr<RawPcieSession>> connect(std::shared_ptr<PcieConnectionContext> context, uint16_t port);

    RawPcieSession() = default;
    virtual ~RawPcieSession();

    virtual hailo_status write(const uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_WRITE_TIMEOUT) override;
    virtual hailo_status read(uint8_t *buffer, size_t size,
        std::chrono::milliseconds timeout = DEFAULT_READ_TIMEOUT) override;
    virtual hailo_status close() override;

    virtual hailo_status wait_for_write_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status write_async(const uint8_t *buffer, size_t size,
        std::function<void(hailo_status)> &&callback) override;

    virtual hailo_status wait_for_read_async_ready(size_t transfer_size, std::chrono::milliseconds timeout) override;
    virtual hailo_status read_async(uint8_t *buffer, size_t size,
        std::function<void(hailo_status)> &&callback) override;

    explicit RawPcieSession(std::shared_ptr<PcieConnectionContext> context) : m_context(context), m_ongoing_writes(0),
        m_ongoing_reads(0) {}

    hailo_status set_session(PcieSession &&session);
    hailo_status connect(uint16_t port);

private:
    hailo_status write_async_aligned(const uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback);
    hailo_status write_async_unaligned(const uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback);
    hailo_status read_async_aligned(uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback);
    hailo_status read_async_unaligned(uint8_t *buffer, size_t size, std::function<void(hailo_status)> &&callback);

    std::mutex m_read_mutex;
    std::condition_variable m_read_cv;
    std::mutex m_write_mutex;
    std::condition_variable m_write_cv;
    std::shared_ptr<PcieConnectionContext> m_context;
    std::shared_ptr<PcieSession> m_session;
    std::atomic_uint32_t m_ongoing_writes;
    std::mutex m_ongoing_writes_mutex;
    std::condition_variable m_ongoing_writes_cv;
    std::atomic_uint32_t m_ongoing_reads;
    std::mutex m_ongoing_reads_mutex;
    std::condition_variable m_ongoing_reads_cv;
};

class RawPcieListener : public SessionListener
{
public:
    static Expected<std::shared_ptr<RawPcieListener>> create_shared(std::shared_ptr<PcieConnectionContext> context, uint16_t port);

    RawPcieListener() = default;
    virtual ~RawPcieListener() = default;

    virtual Expected<std::shared_ptr<Session>> accept() override;

    explicit RawPcieListener(std::shared_ptr<PcieConnectionContext> context, uint16_t port) : SessionListener(port), m_context(context) {}

    hailo_status set_session(PcieSession &&session);

private:

    std::shared_ptr<PcieConnectionContext> m_context;
    std::shared_ptr<PcieSession> m_session;
};

} // namespace hailort

#endif // _PCIE_RAW_CONNECTION_INTERNAL_HPP_