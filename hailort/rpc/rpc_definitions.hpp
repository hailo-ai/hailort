/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_definitions.hpp
 * @brief Common defines used by hailort service and libhailort
 **/

#ifndef _HAILO_RPC_DEFINITIONS_HPP_
#define _HAILO_RPC_DEFINITIONS_HPP_

namespace hailort
{

#ifdef _WIN32
static const std::string HAILORT_SERVICE_DEFAULT_ADDR = "127.0.0.1:50051";
#else
static const std::string HAILO_UDS_PREFIX = "unix://";
static const std::string HAILO_DEFAULT_SERVICE_ADDR = "/tmp/hailort_uds.sock";
static const std::string HAILORT_SERVICE_DEFAULT_ADDR = HAILO_UDS_PREFIX + HAILO_DEFAULT_SERVICE_ADDR;
#endif
static const std::chrono::seconds HAILO_KEEPALIVE_INTERVAL(2);

}

#endif