/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "common/ethernet_utils.hpp"

#include "common/socket.hpp"
#include "common/os/windows/string_conversion.hpp"
#include "hailo/hailort.h"
#include "common/logger_macros.hpp"
#include "hailo/buffer.hpp"

#include <array>
#include <iphlpapi.h>

namespace hailort
{

NetworkInterface::NetworkInterface(uint32_t index, const std::string& name, const std::string& friendly_name, const std::string& ip) :
    m_index(index),
    m_name(name),
    m_friendly_name(friendly_name),
    m_ip(ip)
{}

uint32_t NetworkInterface::index() const
{
    return m_index;
}

std::string NetworkInterface::name() const
{
    return m_name;
}

std::string NetworkInterface::friendly_name() const
{
    return m_friendly_name;
}

std::string NetworkInterface::ip() const
{
    return m_ip;
}

Expected<NetworkInterfaces> NetworkInterface::get_all_interfaces()
{
    static const ULONG IPV4 = AF_INET;
    static const ULONG UNICAST_ONLY = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    static const PVOID RESERVED = nullptr;
    static const PIP_ADAPTER_ADDRESSES NO_INTERFACE_INFO = nullptr;
    ULONG required_size = 0;
    ULONG ret_value = GetAdaptersAddresses(IPV4, UNICAST_ONLY, RESERVED, NO_INTERFACE_INFO, &required_size);
    if (ret_value != ERROR_BUFFER_OVERFLOW) {
        LOGGER__ERROR("Failed calculating necessary size for IP_ADAPTER_ADDRESSES. "
                      "Expected ret_value=ERROR_BUFFER_OVERFLOW, received={}", ret_value);
        return make_unexpected(HAILO_UNEXPECTED_INTERFACE_INFO_FAILURE);
    }

    TRY(auto interface_info_buffer, Buffer::create(required_size, 0));
    ret_value = GetAdaptersAddresses(IPV4, UNICAST_ONLY, RESERVED,
        interface_info_buffer.as_pointer<IP_ADAPTER_ADDRESSES>(), &required_size);
    if (ret_value == ERROR_NO_DATA) {
        LOGGER__ERROR("No IPv4 interfaces found");
        return make_unexpected(HAILO_NO_IPV4_INTERFACES_FOUND);
    } else if (ret_value != NO_ERROR) {
        LOGGER__ERROR("GetInterfaceInfo failed with error: {}", ret_value);
        return make_unexpected(HAILO_UNEXPECTED_INTERFACE_INFO_FAILURE);
    }

    NetworkInterfaces interfaces;
    PIP_ADAPTER_ADDRESSES interface_info = interface_info_buffer.as_pointer<IP_ADAPTER_ADDRESSES>();

    while (interface_info != nullptr) {
        PIP_ADAPTER_UNICAST_ADDRESS first_unicast_address = interface_info->FirstUnicastAddress;
        // TODO: keep a vector of all addresses
        if (first_unicast_address == nullptr) {
            LOGGER__DEBUG("first_unicast_address is null. Skipping.");
            continue;
        }

        const auto address_struct = first_unicast_address->Address.lpSockaddr;
        if ((address_struct == nullptr) || (address_struct->sa_family != AF_INET)) {
            LOGGER__DEBUG("Unicast address is invalid. Skipping.");
            continue;
        }

        TRY(auto ip, Buffer::create(IPV4_STRING_MAX_LENGTH));
        const auto result = Socket::ntop(AF_INET, &(reinterpret_cast<sockaddr_in *>(address_struct)->sin_addr),
            ip.as_pointer<char>(), EthernetUtils::MAX_INTERFACE_SIZE);
        if (result != HAILO_SUCCESS) {
            LOGGER__DEBUG("Failed converting unicast address to string (result={}). Skipping.", result);
            continue;
        }
        const auto friendly_name_ansi = StringConverter::utf16_to_ansi(interface_info->FriendlyName);
        if (!friendly_name_ansi.has_value()) {
            LOGGER__DEBUG("Failed converting the interface's friendly_name to ansi (result={}). Skipping.",
                friendly_name_ansi.status());
            continue;
        }
        interfaces.emplace_back(interface_info->IfIndex, interface_info->AdapterName,
            friendly_name_ansi.value(), ip.to_string());
        
        interface_info = interface_info->Next;
    }
    return interfaces;
}

ArpTable::ArpTable(const std::unordered_map<uint32_t, MacAddress>& table) :
    m_table(table)
{}

Expected<MacAddress> ArpTable::get_mac_address(uint32_t ip) const
{
    auto search = m_table.find(ip);
    if (search == m_table.end()) {
        return make_unexpected(HAILO_MAC_ADDRESS_NOT_FOUND);
    }
    
    return Expected<MacAddress>(m_table.at(ip));
}
    
Expected<ArpTable> ArpTable::create(uint32_t interface_index)
{
    static const PMIB_IPNETTABLE NO_NETTABLE = nullptr;
    static const BOOL SORTED = true;
    ULONG required_size = 0;
    ULONG ret_value = GetIpNetTable(NO_NETTABLE, &required_size, SORTED);
    if (ret_value != ERROR_INSUFFICIENT_BUFFER) {
        LOGGER__ERROR("Failed calculating necessary size for MIB_IPNETTABLE. Expected ret_value=ERROR_INSUFFICIENT_BUFFER, received={}", ret_value);
        return make_unexpected(HAILO_UNEXPECTED_ARP_TABLE_FAILURE);
    }

    TRY(auto ip_net_table_buffer, Buffer::create(required_size, 0));
    ret_value = GetIpNetTable(ip_net_table_buffer.as_pointer<MIB_IPNETTABLE>(), &required_size, SORTED);
    if (ret_value == ERROR_NO_DATA) {
        LOGGER__ERROR("No IPv4 interfaces found");
        return make_unexpected(HAILO_NO_IPV4_INTERFACES_FOUND);
    } else if (ret_value != NO_ERROR) {
        LOGGER__ERROR("GetIpNetTable failed with error: {}", ret_value);
        return make_unexpected(HAILO_UNEXPECTED_ARP_TABLE_FAILURE);
    }

    std::unordered_map<uint32_t, MacAddress> result;
    const PMIB_IPNETTABLE ip_net_table = ip_net_table_buffer.as_pointer<MIB_IPNETTABLE>();
    for (uint32_t i = 0; i < ip_net_table->dwNumEntries; i++) {
        if (ip_net_table->table[i].dwIndex != interface_index) {
            continue;
        }

        if (ip_net_table->table[i].dwPhysAddrLen != MacAddressSize) {
            continue;
        }

        const uint32_t ip = ip_net_table->table[i].dwAddr;
        MacAddress mac{};
        memcpy(mac.data(), ip_net_table->table[i].bPhysAddr, MacAddressSize);
        result[ip] = mac;
    }
    return result;
}

Expected<std::string> EthernetUtils::get_interface_from_board_ip(const std::string &board_ip)
{
    TRY(const auto network_interfaces, NetworkInterface::get_all_interfaces());

    struct in_addr board_ip_struct{};
    auto status = Socket::pton(AF_INET, board_ip.c_str(), &board_ip_struct);
    CHECK_SUCCESS_AS_EXPECTED(status, "Invalid board ip address {}", board_ip);

    for (const auto &network_interface : network_interfaces) {
        TRY(const auto arp_table, ArpTable::create(network_interface.index()));
        const auto mac_address = arp_table.get_mac_address(static_cast<uint32_t>(board_ip_struct.S_un.S_addr));
        if (mac_address) {
            return network_interface.friendly_name();
        }
    }

    return make_unexpected(HAILO_ETH_INTERFACE_NOT_FOUND);
}

Expected<std::string> EthernetUtils::get_ip_from_interface(const std::string &interface_name)
{
    TRY(const auto network_interfaces, NetworkInterface::get_all_interfaces());

    for (const auto &network_interface : network_interfaces) {
        if (network_interface.friendly_name() == interface_name) {
            return network_interface.ip();
        }
    }

    return make_unexpected(HAILO_ETH_INTERFACE_NOT_FOUND);
}

} /* namespace hailort */
