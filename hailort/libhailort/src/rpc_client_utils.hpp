/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_common.hpp
 * @brief Utility functions for rpc client communication
 **/

#ifndef _HAILO_HAILORT_RPC_CLIENT_UTILS_HPP_
#define _HAILO_HAILORT_RPC_CLIENT_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailort_defaults.hpp"
#include "common/async_thread.hpp"
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

    hailo_status init_client_service_communication()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_initialized) {

            auto channel = grpc::CreateChannel(hailort::HAILO_DEFAULT_UDS_ADDR, grpc::InsecureChannelCredentials());
            auto client = make_shared_nothrow<HailoRtRpcClient>(channel);
            CHECK(client != nullptr, HAILO_OUT_OF_HOST_MEMORY);
            m_initialized = true;
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

            m_keep_alive_thread  = make_unique_nothrow<AsyncThread<hailo_status>>([client] () {
                auto pid = getpid();
                auto status = client->client_keep_alive(pid);
                CHECK_SUCCESS(status);
                return HAILO_SUCCESS;
            });

        }
        return HAILO_SUCCESS;
    }

private:
    ~HailoRtRpcClientUtils()
    {
        m_keep_alive_thread.release();
    }

    std::mutex m_mutex;
    AsyncThreadPtr<hailo_status> m_keep_alive_thread;
    bool m_initialized = false;
};

} /* namespace hailort */

#endif /* _HAILO_HAILORT_RPC_CLIENT_UTILS_HPP_ */