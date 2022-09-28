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

static const std::string HAILO_UDS_PREFIX = "unix://";
static const std::string HAILO_DEFAULT_SERVICE_ADDR = "/tmp/hailort_uds.sock";
static const std::string HAILO_DEFAULT_UDS_ADDR = HAILO_UDS_PREFIX + HAILO_DEFAULT_SERVICE_ADDR;
static const uint32_t HAILO_KEEPALIVE_INTERVAL_SEC = 2;

}

#endif