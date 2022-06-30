#!/usr/bin/env python
from builtins import str
import netifaces as ni

from netaddr import IPAddress, IPNetwork


# As defined in sockios.h
SIOCGIFTXQLEN = 0x8942
# Interface name is 16 bytes (including NULL)
SIOCGIFTXQLEN_FMT = "16sI"

class NoInterfaceError(Exception):
    """Raised by get_interface_from_ip when no matching interface was found"""
    pass

def get_interface_from_ip(ip_address):
    """Returns the interface name associated with the given ip addressself.

    Args:
        ip_address (str): the IP address to query.

    Returns:
        str: The name of the interface matching the given IP address.
    """

    skipped_ifaces = []
    for interface in ni.interfaces():
        if ni.AF_INET not in ni.ifaddresses(interface):
            skipped_ifaces.append(interface)
            continue
        af_inet_values = ni.ifaddresses(interface)[ni.AF_INET][0]
        ip_addr, netmask = af_inet_values['addr'], af_inet_values['netmask']
        if is_ip_in_network(ip_addr, netmask, ip_address):
            return str(interface)

    raise NoInterfaceError('No interface for {} found among {}'.format(ip_address, skipped_ifaces))


def get_interface_address(interface_name):
    """Returns the interface address associated with the given interface name.

        Args:
            interface_name (str): the name of the interface to query.

        Returns:
            str: The IP address of the interface matching the given interface_name.
        """
    af_inet_values = ni.ifaddresses(interface_name)[ni.AF_INET][0]
    return af_inet_values['addr']


def is_ip_in_network(network_ip, netmask, ip_in_question):
    """Checks whether an IP address is located in a given network.

    Args:
        network_ip (str): the IP address of the network interface.
        netmask (str): the netmask of the given networkself.
        ip_in_question (str): the IP address to compare against the network.

    Returns:
        bool: whether the IP address belongs to the given network.
    """

    netmask_bits = IPAddress(netmask).netmask_bits()
    return IPAddress(ip_in_question) in IPNetwork('{}/{}'.format(network_ip, netmask_bits))
