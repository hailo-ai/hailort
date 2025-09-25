#!/usr/bin/env python
"""Tool for limiting the packet sending rate via UDP. Needed to ensure the board will not get more
traffic than it can handle, which would cause packet loss.
"""
from __future__ import division
from builtins import object


from hailo_platform.pyhailort.pyhailort import HEF, NetworkRateLimiter, INPUT_DATAFLOW_BASE_PORT

DEFAULT_MAX_KBPS = 850e3

BYTES_IN_Kbits = 125.0


class RateLimiterException(Exception):
    """A problem has occurred during the rate setting."""
    pass

class BadTCParamError(Exception):
    """One of shell's tc command params is wrong."""
    pass


class BadTCCallError(Exception):
    """Shell's tc command has failed."""
    pass


class RateLimiterWrapper(object):
    """UDPRateLimiter wrapper enabling ``with`` statements."""
    def __init__(self, configured_network_group, fps=1, fps_factor=1.0, remote_ip=None):
        """RateLimiterWrapper constructor.

        Args:
            configured_network_group (:class:`~hailo_platform.pyhailort.pyhailort.ConfiguredNetwork`): The
                target network_group.
            fps (int): Frame rate.
            fps_factor (float): Safety factor by which to multiply the calculated UDP rate.
            remote_ip (str): Device IP address.
        """
        self._network_group = configured_network_group
        if remote_ip is None:
            raise RateLimiterException("In order to use RateLimiterWrapper, one should pass 'remote_ip'")
        self._remote_ip = remote_ip
        self._fps = fps
        self._fps_factor = fps_factor
        self._rates_dict = {}
        self._tc_dict = {}

    def __enter__(self):
        max_supported_kbps_rate = DEFAULT_MAX_KBPS

        self._rates_dict = self._network_group.get_udp_rates_dict((self._fps * self._fps_factor),
            (max_supported_kbps_rate * BYTES_IN_Kbits))
        for port, rate in self._rates_dict.items():
            self._tc_dict[port] = NetworkRateLimiter(self._remote_ip, port, rate)
            self._tc_dict[port].reset_rate_limit()
            self._tc_dict[port].set_rate_limit()

    def __exit__(self, *args, **kwargs):
        for tc in self._tc_dict.values():
            tc.reset_rate_limit()
        return False


class UDPRateLimiter(object):
    """Enables limiting or removing limits on UDP communication rate to a board."""
    def __init__(self, remote_ip, port, rate_kbits_per_sec = 0):
        self._tc = NetworkRateLimiter(remote_ip, port, rate_kbits_per_sec * BYTES_IN_Kbits)
    
    def set_rate_limit(self):
        return self._tc.set_rate_limit()

    def reset_rate_limit(self):
        return self._tc.reset_rate_limit()

    @staticmethod    
    def calc_udp_rate(hef, network_group_name, fps, fps_factor=1, max_supported_kbps_rate=850e3):
        """Calculates the proper UDP rate according to an HEF.

        Args:
            hef (str): Path to an HEF file containing the network_group.
            network_group_name (str): Name of the network_group to configure rates for.
            fps (int): Frame rate.
            fps_factor (float, optional): Safety factor by which to multiply the calculated UDP
                rate.
            max_supported_kbps_rate (int, optional): Max supported Kbits per second. Defaults to 850
                Mbit/s (850,000 Kbit/s).

        Returns:
            dict: Maps between each input default dport to its calculated Rate in Kbits/sec.
        """
        hef_object = HEF(hef)
        input_stream_infos = hef_object.get_input_stream_infos(network_group_name)

        input_rates = hef_object.get_udp_rates_dict(int(fps * fps_factor), (max_supported_kbps_rate * BYTES_IN_Kbits),
            network_group_name)

        if len(input_stream_infos) != len(input_rates.keys()):
            raise RateLimiterException("There is a missmatch between the calculated rates and the network inputs.")
        
        results = {}
        for stream_info in input_stream_infos:
            port = stream_info.sys_index + INPUT_DATAFLOW_BASE_PORT
            results[port] = input_rates[stream_info.name] / BYTES_IN_Kbits

        return results