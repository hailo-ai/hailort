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

#define HAILORT_SERVICE_ADDRESS_ENV_VAR ("HAILORT_SERVICE_ADDRESS")
static const std::string HAILORT_SERVICE_ADDRESS = []() {
    const char* env_var = std::getenv(HAILORT_SERVICE_ADDRESS_ENV_VAR);
    if (env_var) {
        return std::string(env_var);
    } else {
        return HAILORT_SERVICE_DEFAULT_ADDR; // Default value if environment variable is not set
    }
}();

class VDeviceIdentifier {
public:
    VDeviceIdentifier(uint32_t vdevice_handle) : m_vdevice_handle(vdevice_handle)
    {}

    bool equals(const VDeviceIdentifier &other)
    {
        return (this->m_vdevice_handle == other.m_vdevice_handle);
    }

    uint32_t m_vdevice_handle;
};

class NetworkGroupIdentifier {
public:
    NetworkGroupIdentifier(VDeviceIdentifier vdevice_identifier, uint32_t network_group_handle) :
        m_vdevice_identifier(vdevice_identifier),
        m_network_group_handle(network_group_handle)
    {}

    bool equals(const NetworkGroupIdentifier &other)
    {
        return ((this->m_vdevice_identifier.equals(other.m_vdevice_identifier)) &&
            (this->m_network_group_handle == other.m_network_group_handle));
    }

    VDeviceIdentifier m_vdevice_identifier;
    uint32_t m_network_group_handle;
};

class VStreamIdentifier {
public:
    VStreamIdentifier(NetworkGroupIdentifier network_group_identifier, uint32_t vstream_handle) :
        m_network_group_identifier(network_group_identifier),
        m_vstream_handle(vstream_handle)
    {}

    bool equals(const VStreamIdentifier &other)
    {
        return ((this->m_network_group_identifier.equals(other.m_network_group_identifier)) &&
            (this->m_vstream_handle == other.m_vstream_handle));
    }

    NetworkGroupIdentifier m_network_group_identifier;
    uint32_t m_vstream_handle;
};

}

#endif