/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file ethernet_utils.hpp
 * @brief TODO
 *
 * TODO
 **/

#ifndef __OS_ETHERNET_UTILS_H__
#define __OS_ETHERNET_UTILS_H__

#include <string>
#include <hailo/hailort.h>
#include "hailo/expected.hpp"

#if defined(_MSC_VER)

#include <unordered_map>
#include <ifmib.h>

namespace hailort
{

class NetworkInterface;
using NetworkInterfaces = std::vector<NetworkInterface>;

class NetworkInterface final
{
public:
    NetworkInterface(uint32_t index, const std::string& name, const std::string& friendly_name, const std::string& ip);
    ~NetworkInterface() = default;

    uint32_t index() const;
    std::string name() const;
    std::string friendly_name() const;
    std::string ip() const;
    
    static Expected<NetworkInterfaces> get_all_interfaces();

private:
    const uint32_t m_index;
    const std::string m_name;
    const std::string m_friendly_name;
    const std::string m_ip;    
};

static const uint32_t MacAddressSize = 6;
using MacAddress = std::array<uint8_t, MacAddressSize>;

class ArpTable final
{
public:
    ~ArpTable() = default;
    Expected<MacAddress> get_mac_address(uint32_t ip) const;
    
    static Expected<ArpTable> create(uint32_t interface_index);

private:
    ArpTable(const std::unordered_map<uint32_t, MacAddress>& table);

    std::unordered_map<uint32_t, MacAddress> m_table;
};

} /* namespace hailort */

#else

#include <net/if.h>

#endif

namespace hailort
{

class EthernetUtils final
{
public:
    EthernetUtils() = delete;

    #if defined(_MSC_VER)
    static const uint32_t MAX_INTERFACE_SIZE = MAX_INTERFACE_NAME_LEN;
    #else
    static const uint32_t MAX_INTERFACE_SIZE = IFNAMSIZ;
    #endif

    static hailo_status get_interface_from_board_ip(const char *board_ip, char *interface_name, size_t interface_name_length);
    static hailo_status get_ip_from_interface(const char *interface_name, char *ip, size_t ip_length);

private:
    #if defined(__GNUG__)
    static hailo_status get_interface_from_arp_entry(char *arp_entry, char *interface_name,
        size_t max_interface_name_length);
    #endif
};

} /* namespace hailort */

#endif /* __OS_ETHERNET_UTILS_H__ */
