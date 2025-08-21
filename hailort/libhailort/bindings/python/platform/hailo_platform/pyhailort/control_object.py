#!/usr/bin/env python

"""Control operations for the Hailo hardware device."""

from hailo_platform.common.logger.logger import default_logger
from hailo_platform.pyhailort.pyhailort import (Control, InternalPcieDevice, ExceptionWrapper, BoardInformation,  # noqa F401
                                                CoreInformation, DeviceArchitectureTypes, ExtendedDeviceInformation,  # noqa F401
                                                HealthInformation, SamplingPeriod, AveragingFactor, DvmTypes, # noqa F401
                                                PowerMeasurementTypes, MeasurementBufferIndex) # noqa F401

import hailo_platform.pyhailort._pyhailort as _pyhailort

class ControlObjectException(Exception):
    """Raised on illegal ContolObject operation."""
    pass


class FirmwareUpdateException(Exception):
    pass


class HailoControl(Control):
    """Control object that sends control operations to a Hailo hardware device."""

class HcpControl(HailoControl):
    """Control object that uses the HCP protocol for controlling the device."""

class PcieHcpControl(HcpControl):
    """Control object that uses a HCP over PCIe controller interface."""

    def __init__(self, device=None, device_info=None):
        """Initializes a new HailoPcieController object."""

        default_logger().warning("PcieHcpControl is deprecated! Please Use Control object")
        if device_info is None:
            device_info = InternalPcieDevice.scan_devices()[0]

        if device is None:
            with ExceptionWrapper():
                device = _pyhailort.Device.create_pcie(device_info)
        else:
            # Needs to get the _pyhailort.Device object
            device = device.device

        super().__init__(device)
