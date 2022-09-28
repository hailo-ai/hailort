#!/usr/bin/env python
"""
.. module:: hailo_control_protocol
   :synopsis: Implements a Hailo Control Protocol message.
"""

from hailo_platform.pyhailort.pyhailort import (SUPPORTED_PROTOCOL_VERSION, SUPPORTED_FW_MAJOR, SUPPORTED_FW_MINOR, # noqa F401
    SUPPORTED_FW_REVISION, DeviceArchitectureTypes, ExtendedDeviceInformation, BoardInformation, # noqa F401
    CoreInformation, TemperatureThrottlingLevel, HealthInformation, HailoFirmwareMode, HailoFirmwareType, # noqa F401
    HailoFirmwareVersion, SupportedFeatures) # noqa F401
import warnings
warnings.warn('Importing hailo_control_protocol directly is deprecated. One should use direct hailo_platform instead')
