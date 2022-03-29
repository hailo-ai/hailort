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

namespace hailort
{

#define ETHERNET_UTILS__ARP_FILE ("/proc/net/arp")
#define ETHERNET_UTILS__ARP_ENTRY_DELIMIETERS (" ,\n")
#define ETHERNET_UTILS__ARP_MAX_ENTRY_LENGTH (512)
#define ETHERNET_UTILS__ARP_DEVICE_NAME_INDEX (4)


hailo_status EthernetUtils::get_interface_from_arp_entry(char *arp_entry, char *interface_name,
        size_t max_interface_name_length)
{
    /* This function parses the interface name out from the arp entry
     * Each entry is built as follows:
     *     IP address       HW type     Flags       HW address            Mask     Device 
     *
     * For example:
     *     10.0.0.163       0x1         0x2         80:00:de:ad:be:3f     *        enp1s0
     * */
    hailo_status status = HAILO_UNINITIALIZED;
    size_t token_counter = 0;
    char* token = NULL;

    /* Start splitting the arp entry into tokens according to the delimiter */
    token = strtok(arp_entry, ETHERNET_UTILS__ARP_ENTRY_DELIMIETERS);
    if (NULL == token) {
        LOGGER__ERROR("Invalid arp entry, could not split it to tokens");
        status = HAILO_ETH_FAILURE;
        goto l_exit;
    }

    /* Iterate over the tokens until the device name is found */
    while (NULL != token) {
        token = strtok(NULL, ETHERNET_UTILS__ARP_ENTRY_DELIMIETERS);
        if (ETHERNET_UTILS__ARP_DEVICE_NAME_INDEX == token_counter) {
            LOGGER__DEBUG("Interface name: {}", token);
            strncpy(interface_name, token, max_interface_name_length);
            break;
        }
        token_counter++;
    } 

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

hailo_status EthernetUtils::get_interface_from_board_ip(const char *board_ip, char *interface_name, size_t interface_name_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    FILE* arp_file = NULL;
    int fclose_rc = -1;
    char buffer[ETHERNET_UTILS__ARP_MAX_ENTRY_LENGTH] = {};

    CHECK_ARG_NOT_NULL(interface_name);
    CHECK_ARG_NOT_NULL(board_ip);

    /* Open arp file */
    arp_file = fopen(ETHERNET_UTILS__ARP_FILE, "r");
    if (NULL == arp_file) {
        LOGGER__ERROR("Cannot open file {}. Errno: {:#x}", ETHERNET_UTILS__ARP_FILE, errno);
        status = HAILO_OPEN_FILE_FAILURE;
        goto l_exit;
    }

    /* Go over all of the lines at the file */
    while(fgets(buffer, ARRAY_LENGTH(buffer), arp_file)) {
        /* Check if the arp line contains the board_ip */
        if (strstr(buffer, board_ip)) {
            status = get_interface_from_arp_entry(buffer, interface_name, interface_name_length);
            if (HAILO_SUCCESS != status) {
                goto l_exit;
            }
            break;
        }
    }

    status = HAILO_SUCCESS;
l_exit:
    if (NULL != arp_file) {
        fclose_rc = fclose(arp_file);
        if (0 != fclose_rc) {
            LOGGER__ERROR("Cannot close arp file {} ", ETHERNET_UTILS__ARP_FILE);
            if (HAILO_SUCCESS == status) {
                status = HAILO_CLOSE_FAILURE;
            } else {
                LOGGER__ERROR("Did not override status. Left status value at: {} (not assigned {}",
                        status,
                        HAILO_CLOSE_FAILURE);
            }
        }
    }

    return status;
}

hailo_status EthernetUtils::get_ip_from_interface(const char *interface_name, char *ip, size_t ip_length)
{
    hailo_status status = HAILO_UNINITIALIZED;
    struct ifreq ifr = {};
    int fd = 0;
    int posix_rc = 0;

    CHECK_ARG_NOT_NULL(interface_name);
    CHECK_ARG_NOT_NULL(ip);

    /* Create socket */
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOGGER__ERROR("Failed to create socket. Errno: {:#x}", errno);
        status = HAILO_ETH_FAILURE;
        goto l_exit;
    }

    /* Convert interface name to ip address */
    ifr.ifr_addr.sa_family = AF_INET;
    (void)strncpy(ifr.ifr_name, interface_name, IFNAMSIZ-1);
    posix_rc = ioctl(fd, SIOCGIFADDR, &ifr);
    if (0 > posix_rc) {
        LOGGER__ERROR("Interface was not found. ioctl with SIOCGIFADDR has failed. Errno: {:#x}", errno);
        status = HAILO_ETH_INTERFACE_NOT_FOUND;
        goto l_exit;
    }
    (void)strncpy(ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), ip_length);
    LOGGER__DEBUG("Interface {} | IP: {}", interface_name, ip);

    status = HAILO_SUCCESS;
l_exit:
    /* Close the socket if it was created */
    if (0 < fd) {
        posix_rc = close(fd);
        if (0 != posix_rc) {
            LOGGER__ERROR("Failed closing socket. Errno: {:#x}", errno);
            /* Update status if only in case there was not previous error */
            if (HAILO_SUCCESS == status) {
                status = HAILO_CLOSE_FAILURE;
            } else {
                LOGGER__ERROR("Did not override status. Left status value at: {} (not assigned {}",
                        status,
                        HAILO_CLOSE_FAILURE);
            }
        }
    }

    return status;
}

} /* namespace hailort */
