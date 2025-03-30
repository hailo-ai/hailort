/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include "utils.h"

#include <hailo/hailort.h>
#include "common/utils.hpp"
#include "common/logger_macros.hpp"
#include "common/ethernet_utils.hpp"
#include "common/socket.hpp"

#include <fstream>

namespace hailort
{

#define ETHERNET_UTILS__ARP_FILE ("/proc/net/arp")
#define ETHERNET_UTILS__ARP_ENTRY_DELIMIETERS (" ,\n")
#define ETHERNET_UTILS__ARP_MAX_ENTRY_LENGTH (512)
#define ETHERNET_UTILS__ARP_DEVICE_NAME_INDEX (4)


Expected<std::string> EthernetUtils::get_interface_from_arp_entry(char *arp_entry)
{
    /* This function parses the interface name out from the arp entry
     * Each entry is built as follows:
     *     IP address       HW type     Flags       HW address            Mask     Device 
     *
     * For example:
     *     10.0.0.163       0x1         0x2         80:00:de:ad:be:3f     *        enp1s0
     * */
    size_t token_counter = 0;
    char* token = NULL;

    /* Start splitting the arp entry into tokens according to the delimiter */
    token = strtok(arp_entry, ETHERNET_UTILS__ARP_ENTRY_DELIMIETERS);
    CHECK_AS_EXPECTED(nullptr != token, HAILO_ETH_FAILURE, "Invalid arp entry, could not split it to tokens");

    /* Iterate over the tokens until the device name is found */
    while (NULL != token) {
        token = strtok(NULL, ETHERNET_UTILS__ARP_ENTRY_DELIMIETERS);
        if (ETHERNET_UTILS__ARP_DEVICE_NAME_INDEX == token_counter) {
            LOGGER__DEBUG("Interface name: {}", token);
            return std::string(token);
        }
        token_counter++;
    } 

    return make_unexpected(HAILO_ETH_FAILURE);
}

Expected<std::string> EthernetUtils::get_interface_from_board_ip(const std::string &board_ip)
{
    std::ifstream arp_file(ETHERNET_UTILS__ARP_FILE, std::ios::in);
    CHECK_AS_EXPECTED(arp_file, HAILO_OPEN_FILE_FAILURE, "Cannot open file {}. errno: {:#x}", ETHERNET_UTILS__ARP_FILE, errno);

    char buffer[ETHERNET_UTILS__ARP_MAX_ENTRY_LENGTH] = {};

    /* Go over all of the lines at the file */
    while (arp_file.getline(buffer, sizeof(buffer))) {
        if (strstr(buffer, board_ip.c_str())) {
            return get_interface_from_arp_entry(buffer);
        }
    }

    LOGGER__ERROR("Failed to find interface name for ip {}", board_ip);
    return make_unexpected(HAILO_ETH_FAILURE);
}

Expected<std::string> EthernetUtils::get_ip_from_interface(const std::string &interface_name)
{
    struct ifreq ifr = {};

    /* Create socket */
    TRY(const auto socket, Socket::create(AF_INET, SOCK_DGRAM, 0));

    /* Convert interface name to ip address */
    ifr.ifr_addr.sa_family = AF_INET;
    (void)strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ-1);
    auto posix_rc = ioctl(socket.get_fd(), SIOCGIFADDR, &ifr);
    CHECK_AS_EXPECTED(posix_rc >= 0, HAILO_ETH_INTERFACE_NOT_FOUND,
        "Interface was not found. ioctl with SIOCGIFADDR has failed. errno: {:#x}", errno);

    std::string res = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
    LOGGER__DEBUG("Interface {} | IP: {}", interface_name, res);
    return res;
}

} /* namespace hailort */
