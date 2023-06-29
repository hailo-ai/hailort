/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_client_utils.hpp
 * @brief Utility functions for rpc client communication
 **/

#ifndef _HAILO_HAILORT_RPC_CLIENT_UTILS_HPP_
#define _HAILO_HAILORT_RPC_CLIENT_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/async_thread.hpp"
#include "common/os_utils.hpp"

#include "hailort_rpc_client.hpp"
#include "rpc/rpc_definitions.hpp"

#include <chrono>

namespace hailort
{

class HailoRtRpcClientUtils final
{
public:
    static HailoRtRpcClientUtils& get_instance()
    {
        static HailoRtRpcClientUtils instance;
        return instance;
    }

    HailoRtRpcClientUtils() :
        m_mutex(std::make_shared<std::mutex>())
    {
        auto status = init_keep_alive_shutdown_event();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to initialize RPC Client's keep-alive shutdown event with status {}", status);
        }
    }

    static Expected<std::unique_ptr<HailoRtRpcClient>> create_client()
    {
        auto channel = grpc::CreateChannel(HAILORT_SERVICE_DEFAULT_ADDR, grpc::InsecureChannelCredentials());
        CHECK_AS_EXPECTED(channel != nullptr, HAILO_INTERNAL_FAILURE);
        auto client = make_unique_nothrow<HailoRtRpcClient>(channel);
        CHECK_NOT_NULL_AS_EXPECTED(client, HAILO_INTERNAL_FAILURE);
        return client;
    }

    hailo_status init_client_service_communication()
    {
        std::unique_lock<std::mutex> lock(*m_mutex);
        if (!m_initialized) {
            // Create client
            auto channel = grpc::CreateChannel(hailort::HAILORT_SERVICE_DEFAULT_ADDR, grpc::InsecureChannelCredentials());
            auto client = make_unique_nothrow<HailoRtRpcClient>(channel);
            CHECK_NOT_NULL(client, HAILO_OUT_OF_HOST_MEMORY);

            // Check service version
            auto reply = client->get_service_version();
            CHECK_EXPECTED_AS_STATUS(reply);
            hailo_version_t client_version = {};
            auto status = hailo_get_library_version(&client_version);
            CHECK_SUCCESS(status);
            auto service_version = reply.value();
            auto are_equal = [](auto version1, auto version2) {
                return version1.major == version2.major
                    && version1.minor == version2.minor
                    && version1.revision == version2.revision;
            };
            CHECK(are_equal(service_version, client_version), HAILO_INVALID_SERVICE_VERSION, "Invalid libhailort version on service: "
                "client version {}.{}.{}, service version {}.{}.{}",
                service_version.major, service_version.minor, service_version.revision,
                client_version.major, client_version.minor, client_version.revision);

            // Set pid
            m_pid = OsUtils::get_curr_pid();

            // Trigger client keep-alive
            status = start_keep_alive_thread();
            CHECK_SUCCESS(status);

            m_initialized = true;
        }
        return HAILO_SUCCESS;
    }

    void before_fork()
    {
        stop_keep_alive_thread();
    }

    hailo_status after_fork_in_parent()
    {
        m_keep_alive_shutdown_event->reset();
        std::unique_lock<std::mutex> lock(*m_mutex);
        if (m_initialized) {
            return start_keep_alive_thread();
        }
        return HAILO_SUCCESS;
    }

    hailo_status after_fork_in_child()
    {
        m_mutex = std::make_shared<std::mutex>();
        auto status = init_keep_alive_shutdown_event();
        CHECK_SUCCESS(status);

        std::unique_lock<std::mutex> lock(*m_mutex);
        if (m_initialized) {
            m_pid = OsUtils::get_curr_pid();
            return start_keep_alive_thread();
        }
        return HAILO_SUCCESS;
    }

private:
    ~HailoRtRpcClientUtils()
    {
        stop_keep_alive_thread();
    }

    void stop_keep_alive_thread()
    {
        if (m_keep_alive_shutdown_event) {
            (void)m_keep_alive_shutdown_event->signal();
        }

        m_keep_alive_thread.reset();
    }

    hailo_status start_keep_alive_thread()
    {
        m_keep_alive_thread = make_unique_nothrow<AsyncThread<hailo_status>>("SVC_KEEPALIVE", [this] () {
            return this->keep_alive();
        });
        CHECK_NOT_NULL(m_keep_alive_thread, HAILO_OUT_OF_HOST_MEMORY);
        return HAILO_SUCCESS;
    }

    hailo_status keep_alive()
    {
        auto channel = grpc::CreateChannel(hailort::HAILORT_SERVICE_DEFAULT_ADDR, grpc::InsecureChannelCredentials());
        auto client = make_unique_nothrow<HailoRtRpcClient>(channel);
        CHECK_NOT_NULL(client, HAILO_OUT_OF_HOST_MEMORY);

        while (true) {
            auto shutdown_status = m_keep_alive_shutdown_event->wait(hailort::HAILO_KEEPALIVE_INTERVAL / 2);
            if (HAILO_TIMEOUT != shutdown_status) {
                // shutdown event is signal (or we have another error)
                return shutdown_status;
            }

            // keep alive interval
            auto status = client->client_keep_alive(m_pid);
            CHECK_SUCCESS(status);
        }
    }

    hailo_status init_keep_alive_shutdown_event()
    {
        m_keep_alive_shutdown_event = Event::create_shared(Event::State::not_signalled);
        CHECK(nullptr != m_keep_alive_shutdown_event, HAILO_OUT_OF_HOST_MEMORY);

        return HAILO_SUCCESS;
    }

    std::shared_ptr<std::mutex> m_mutex;
    AsyncThreadPtr<hailo_status> m_keep_alive_thread;
    bool m_initialized = false;
    uint32_t m_pid;
    EventPtr m_keep_alive_shutdown_event;
};

} /* namespace hailort */

#endif /* _HAILO_HAILORT_RPC_CLIENT_UTILS_HPP_ */