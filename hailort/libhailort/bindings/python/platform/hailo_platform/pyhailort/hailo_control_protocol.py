#!/usr/bin/env python
"""
.. module:: hailo_control_protocol
   :synopsis: Implements a Hailo Control Protocol message.
"""

from builtins import object
from enum import Enum, IntEnum

import struct

# Supported protocol and Firmware version of current SDK.
SUPPORTED_PROTOCOL_VERSION = 2
SUPPORTED_FW_MAJOR = 4
SUPPORTED_FW_MINOR = 7
SUPPORTED_FW_REVISION = 0

MEGA_MULTIPLIER = 1000.0 * 1000.0


class HailoControlProtocolException(Exception):
    pass


class DeviceArchitectureTypes(IntEnum):
    HAILO8_A0 = 0
    HAILO8_B0 = 1
    MERCURY_CA = 2

    def __str__(self):
        return self.name

class BoardInformation(object):
    def __init__(self, protocol_version, fw_version_major, fw_version_minor, fw_version_revision,
                 logger_version, board_name, is_release, device_architecture, serial_number, part_number, product_name):
        self.protocol_version = protocol_version
        self.firmware_version = HailoFirmwareVersion.construct_from_params(fw_version_major, fw_version_minor, fw_version_revision, is_release, HailoFirmwareType.APP)
        self.logger_version = logger_version
        self.board_name = board_name
        self.is_release = is_release
        self.device_architecture = DeviceArchitectureTypes(device_architecture)
        self.serial_number = serial_number
        self.part_number = part_number
        self.product_name = product_name
    
    def _string_field_str(self, string_field):
        # Return <Not Configured> if the string field is empty
        return string_field.rstrip('\x00') or "<Not Configured>"

    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        return 'Control Protocol Version: {}\n' \
               'Firmware Version: {}\n' \
               'Logger Version: {}\n' \
               'Board Name: {}\n' \
               'Device Architecture: {}\n' \
               'Serial Number: {}\n' \
               'Part Number: {}\n' \
               'Product Name: {}\n'.format(
            self.protocol_version,
            self.firmware_version,
            self.logger_version,
            self.board_name.rstrip('\x00'),
            str(self.device_architecture),
            self._string_field_str(self.serial_number),
            self._string_field_str(self.part_number),
            self._string_field_str(self.product_name))
       
    def __repr__(self):
        """Returns:
            str: Human readable string.
        """ 
        return self.__str__()

    @staticmethod
    def get_hw_arch_str(device_arch):
        if device_arch == DeviceArchitectureTypes.HAILO8_B0:
            return 'hailo8'
        elif device_arch == DeviceArchitectureTypes.MERCURY_CA:
            return 'mercury'
        else:
            raise HailoControlProtocolException("Unsupported device architecture.")

class CoreInformation(object):
    def __init__(self, fw_version_major, fw_version_minor, fw_version_revision, is_release):
        self.firmware_version = HailoFirmwareVersion.construct_from_params(fw_version_major, fw_version_minor, fw_version_revision, is_release, HailoFirmwareType.CORE)
        self.is_release = is_release
    
    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        return 'Core Firmware Version: {}'.format(
            self.firmware_version)

    def __repr__(self):
        """Returns:
            str: Human readable string.
        """
        return self.__str__()

class TemperatureThrottlingLevel(object):
    def __init__(self, level_number, temperature_threshold, hysteresis_temperature_threshold, throttling_nn_clock_freq):
        self.level_number = level_number
        self.temperature_threshold = temperature_threshold
        self.hysteresis_temperature_threshold = hysteresis_temperature_threshold
        self.throttling_nn_clock_freq = throttling_nn_clock_freq

    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        return 'Temperature Throttling Level {}: \n' \
               'Temperature Threshold: {}\n' \
               'Hysteresis Temperature Threshold: {}\n' \
               'Throttling NN Clock Frequency: {}\n' \
               .format(self.level_number, self.temperature_threshold, self.hysteresis_temperature_threshold, self.throttling_nn_clock_freq)
        
    def __repr__(self):
        return self.__str__()

class HealthInformation(object):
    def __init__(self, overcurrent_protection_active, current_overcurrent_zone, red_overcurrent_threshold, orange_overcurrent_threshold, 
                       temperature_throttling_active, current_temperature_zone, current_temperature_throttling_level,
                       temperature_throttling_levels, orange_temperature_threshold, orange_hysteresis_temperature_threshold, 
                       red_temperature_threshold, red_hysteresis_temperature_threshold):
        self.overcurrent_protection_active = overcurrent_protection_active
        self.current_overcurrent_zone = current_overcurrent_zone
        self.red_overcurrent_threshold = red_overcurrent_threshold
        self.orange_overcurrent_threshold = orange_overcurrent_threshold
        self.temperature_throttling_active = temperature_throttling_active
        self.current_temperature_zone = current_temperature_zone
        self.current_temperature_throttling_level = current_temperature_throttling_level
        self.orange_temperature_threshold = orange_temperature_threshold
        self.orange_hysteresis_temperature_threshold = orange_hysteresis_temperature_threshold
        self.red_temperature_threshold = red_temperature_threshold
        self.red_hysteresis_temperature_threshold = red_hysteresis_temperature_threshold
        
        # Add TemperatureThrottlingLevel in case it has new throttling_nn_clock_freq. level_number can be used as only last
        # levels can be with the same freq
        self.temperature_throttling_levels = []
        if self.temperature_throttling_active:
            throttling_nn_clock_frequencies = []
            for level_number, temperature_throttling_level in enumerate(temperature_throttling_levels):
                if temperature_throttling_level.throttling_nn_clock_freq not in throttling_nn_clock_frequencies:
                    throttling_nn_clock_frequencies.append(temperature_throttling_level.throttling_nn_clock_freq)
                    self.temperature_throttling_levels.append(TemperatureThrottlingLevel(level_number,
                                                                                        temperature_throttling_level.temperature_threshold, 
                                                                                        temperature_throttling_level.hysteresis_temperature_threshold, 
                                                                                        temperature_throttling_level.throttling_nn_clock_freq))
    def __repr__(self):
        return self.__str__()

    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        temperature_throttling_levels_str = "\n".join(["\n\n{}\n".format(str(temperature_throttling_level)) for temperature_throttling_level in self.temperature_throttling_levels]) \
                                            if self.temperature_throttling_active else "<Temperature throttling is disabled>"
        return 'Overcurrent Protection Active: {}\n' \
               'Overcurrent Protection Current Overcurrent Zone: {}\n' \
               'Overcurrent Protection Red Threshold: {}\n' \
               'Overcurrent Protection Orange Threshold: {}\n' \
               'Temperature Protection Red Threshold: {}\n' \
               'Temperature Protection Red Hysteresis Threshold: {}\n' \
               'Temperature Protection Orange Threshold: {}\n' \
               'Temperature Protection Orange Hysteresis Threshold: {}\n' \
               'Temperature Protection Throttling State: {}\n' \
               'Temperature Protection Current Zone: {}\n' \
               'Temperature Protection Current Throttling Level: {}\n' \
               'Temperature Protection Throttling Levels: {}' \
               .format(self.overcurrent_protection_active, self.current_overcurrent_zone, self.red_overcurrent_threshold, 
                       self.orange_overcurrent_threshold, self.red_temperature_threshold, 
                       self.red_hysteresis_temperature_threshold, self.orange_temperature_threshold, 
                       self.orange_hysteresis_temperature_threshold, self.temperature_throttling_active,
                       self.current_temperature_zone, self.current_temperature_throttling_level, temperature_throttling_levels_str)

class ExtendedDeviceInformation(object):
    def __init__(self, neural_network_core_clock_rate, supported_features, boot_source, lcs, soc_id, eth_mac_address, unit_level_tracking_id, soc_pm_values):
        self.neural_network_core_clock_rate = neural_network_core_clock_rate
        self.supported_features = SupportedFeatures(supported_features)
        self.boot_source = boot_source
        self.lcs = lcs
        self.soc_id = soc_id
        self.eth_mac_address = eth_mac_address
        self.unit_level_tracking_id = unit_level_tracking_id
        self.soc_pm_values = soc_pm_values

    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        string = 'Neural Network Core Clock Rate: {}MHz\n' \
                 '{}' \
                 'Boot source: {}\n' \
                 'LCS: {}\n'.format(
            self.neural_network_core_clock_rate / MEGA_MULTIPLIER,
            str(self.supported_features),
            str(self.boot_source.name),
            str(self.lcs))
        if any(self.soc_id):
            string += 'SoC ID: ' + (self.soc_id.hex())

        if any(self.eth_mac_address):
            string += '\nMAC Address: ' + (":".join("{:02X}".format(i) for i in self.eth_mac_address))
        
        if any(self.unit_level_tracking_id):
            string += '\nULT ID: ' + (self.unit_level_tracking_id.hex())
        
        if any(self.soc_pm_values):
            string += '\nPM Values: ' + (self.soc_pm_values.hex())


        return string

    def __repr__(self):
        """Returns:
            str: Human readable string.
        """
        return self.__str__()

class HailoFirmwareMode(Enum):
    """Indication that firmware version is stable and official  """
    DEVELOP = 'develop'
    RELEASE = 'release'


class HailoFirmwareType(Enum):
    """Indication the firmware type """
    CORE = 'core'
    APP = 'app'


class HailoResetTypes(Enum):
    """Defines the available reset types."""
    CHIP = 'chip'
    NN_CORE = 'nn_core'
    SOFT = 'soft'
    FORCED_SOFT = 'forced_soft'


class HailoFirmwareVersion(object):
    """Represents a Hailo chip firmware version."""
    DEV_BIT  = 0x80000000
    CORE_BIT = 0x08000000
    FW_VERSION_FORMAT = '<III'

    def __init__(self, firmware_version_buffer, is_release, fw_type):
        """Initialize a new Hailo Firmware Version object.

        Args:
            firmware_version_buffer (str): A buffer containing the firmware version struct.
            is_release (bool, optional): Flag indicating if firmware is at develop/release mode.
                                        None indicates unknown
        """
        self.major, self.minor, self.revision = struct.unpack(
            self.FW_VERSION_FORMAT,
            firmware_version_buffer)
        
        self.fw_type = fw_type
        self.mode = HailoFirmwareMode.RELEASE if is_release else HailoFirmwareMode.DEVELOP
        
        self.revision &= ~(self.CORE_BIT | self.DEV_BIT)

    def __str__(self):
        """Returns:
            str: Firmware version in a human readable format.
        """
        return '{}.{}.{} ({},{})'.format(self.major, self.minor, self.revision, self.mode.value, self.fw_type.value)

    @classmethod
    def construct_from_params(cls, major, minor, revision, is_release, fw_type):
        """Returns:
            class HailoFirmwareVersion : with the given Firmware version.
        """
        return cls(struct.pack(HailoFirmwareVersion.FW_VERSION_FORMAT, major, minor, revision), is_release, fw_type)

    @property
    def comparable_value(self):
        """A value that could be compared to other firmware versions."""
        return (self.major << 64) + (self.minor << 32) + (self.revision)

    def __hash__(self):
        return self.comparable_value

    def __eq__(self, other):
        return self.comparable_value == other.comparable_value

    # TODO: Required for Python2 BW compatibility (SDK-10038)
    # This impl' comes by default in Python3
    def __ne__(self, other):
        return not (self == other)

    def __lt__(self, other):
        return self.comparable_value < other.comparable_value

    def check_protocol_compatibility(self, other):
        return ((self.major == other.major) and (self.minor == other.minor))

class SupportedFeatures(object):
    def __init__(self, supported_features):
        self.ethernet = supported_features.ethernet
        self.mipi = supported_features.mipi
        self.pcie = supported_features.pcie
        self.current_monitoring = supported_features.current_monitoring
        self.mdio = supported_features.mdio
    
    def _feature_str(self, feature_name, is_feature_enabled):
        return '{}: {}\n'.format(feature_name, 'Enabled' if is_feature_enabled else 'Disabled')

    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        return 'Device supported features: \n' + \
            self._feature_str('Ethernet', self.ethernet) + \
            self._feature_str('MIPI', self.mipi) + \
            self._feature_str('PCIE', self.pcie) + \
            self._feature_str('Current Monitoring', self.current_monitoring) + \
            self._feature_str('MDIO', self.mdio)

    def __repr__(self):
        """Returns:
            str: Human readable string.
        """
        return self.__str__()

    def _is_feature_enabled(self, feature):
        return (self.supported_features & feature) != 0
