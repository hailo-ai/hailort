/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file raw_connection_internal.hpp
 * @brief Raw Connection Header for pcie based comunication
 **/

#ifndef _PCIE_RAW_CONNECTION_INTERNAL_HPP_
#define _PCIE_RAW_CONNECTION_INTERNAL_HPP_

#include "hailo/expected.hpp"
#include "vdma/pcie_session.hpp"
#include "hrpc/raw_connection.hpp"

#include <memory>
#include <condition_variable>

using namespace hailort;

namespace hrpc
{

class PcieConnectionContext : public ConnectionContext
{
public:
    static Expected<std::shared_ptr<ConnectionContext>> create_shared(bool is_accepting);

    PcieConnectionContext(std::shared_ptr<HailoRTDriver> &&driver, bool is_accepting,
        Buffer &&write_buffer, Buffer &&read_buffer)
        : ConnectionContext(is_accepting), m_driver(std::move(driver)),
            m_write_buffer(std::move(write_buffer)), m_read_buffer(std::move(read_buffer)),
            m_conn_count(0) {}

    virtual ~PcieConnectionContext() = default;

    std::shared_ptr<HailoRTDriver> driver() { return m_driver; }
    Buffer &write_buffer() { return m_write_buffer; }
    Buffer &read_buffer() { return m_read_buffer; }

    hailo_status wait_for_available_connection();
    void mark_connection_closed();

private:
    std::shared_ptr<HailoRTDriver> m_driver;
    Buffer m_write_buffer;
    Buffer m_read_buffer;
    uint32_t m_conn_count;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

class PcieRawConnection : public RawConnection
{
public:
    static Expected<std::shared_ptr<RawConnection>> create_shared(std::shared_ptr<PcieConnectionContext> context);
    
    PcieRawConnection() = default;
    virtual ~PcieRawConnection() = default;

    virtual Expected<std::shared_ptr<RawConnection>> accept() override;
    virtual hailo_status connect() override;
    virtual hailo_status write(const uint8_t *buffer, size_t size) override;
    virtual hailo_status read(uint8_t *buffer, size_t size) override;
    virtual hailo_status close() override;

    explicit PcieRawConnection(std::shared_ptr<PcieConnectionContext> context) : m_context(context) {}
private:
    hailo_status set_session(PcieSession &&session);

    std::shared_ptr<PcieConnectionContext> m_context;
    std::shared_ptr<PcieSession> m_session;
};

} // namespace hrpc

#endif // _PCIE_RAW_CONNECTION_INTERNAL_HPP_