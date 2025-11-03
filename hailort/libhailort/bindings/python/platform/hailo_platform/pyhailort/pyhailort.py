from enum import Enum, IntEnum
from typing import List
import signal
import struct

import sys

from collections import deque
from dataclasses import dataclass
from argparse import ArgumentTypeError
from datetime import timedelta
import numpy
import time
import gc
import os
import json

from hailo_platform.common.logger.logger import default_logger
import hailo_platform.pyhailort._pyhailort as _pyhailort

from hailo_platform.pyhailort._pyhailort import (TemperatureInfo, # noqa F401
                                                 DvmTypes, PowerMeasurementTypes,  # noqa F401
                                                 PowerMeasurementData, NotificationId,  # noqa F401
                                                 OvercurrentAlertState,
                                                 FormatOrder,
                                                 AveragingFactor, SamplingPeriod, MeasurementBufferIndex,
                                                 FormatType, WatchdogMode,
                                                 BootSource, Endianness,
                                                 SensorConfigTypes)

BBOX_PARAMS = _pyhailort.HailoRTDefaults.BBOX_PARAMS()
BBOX_WITH_MASK_PARAMS = 6  # 4 coordinates + score + class_idx
INPUT_DATAFLOW_BASE_PORT = _pyhailort.HailoRTDefaults.DEVICE_BASE_INPUT_STREAM_PORT()
OUTPUT_DATAFLOW_BASE_PORT = _pyhailort.HailoRTDefaults.DEVICE_BASE_OUTPUT_STREAM_PORT()
PCIE_ANY_DOMAIN = _pyhailort.HailoRTDefaults.PCIE_ANY_DOMAIN()
HAILO_UNIQUE_VDEVICE_GROUP_ID = _pyhailort.HailoRTDefaults.HAILO_UNIQUE_VDEVICE_GROUP_ID()
DEFAULT_VSTREAM_TIMEOUT_MS = 10000
DEFAULT_VSTREAM_QUEUE_SIZE = 2
BOARD_INFO_NOT_CONFIGURED_ATTR = "<N/A>"


class HailoSchedulingAlgorithm(_pyhailort.SchedulingAlgorithm):
    pass

class HailoRTException(Exception):
    pass

class InvalidProtocolVersionException(HailoRTException):
    pass

class HailoRTFirmwareControlFailedException(HailoRTException):
    pass

class HailoRTInvalidFrameException(HailoRTException):
    pass

class HailoRTUnsupportedOpcodeException(HailoRTException):
    pass

class HailoRTTimeout(HailoRTException):
    pass

class HailoRTStreamAborted(HailoRTException):
    pass

class HailoRTStreamAbortedByUser(HailoRTException):
    pass

class HailoRTInvalidOperationException(HailoRTException):
    pass

class HailoRTInvalidArgumentException(HailoRTException):
    pass

class HailoRTNotFoundException(HailoRTException):
    pass

class HailoRTInvalidHEFException(HailoRTException):
    pass

class HailoRTHEFNotCompatibleWithDevice(HailoRTException):
    pass

class HailoRTEthException(HailoRTException):
    pass

class HailoRTDriverOperationFailedException(HailoRTException):
    pass

# HailoRTPCIeDriverException is deprecated, use HailoRTDriverOperationFailedException instead
HailoRTPCIeDriverException = HailoRTDriverOperationFailedException

class HailoRTNetworkGroupNotActivatedException(HailoRTException):
    pass

class HailoCommunicationClosedException(HailoRTException):
    pass

class HailoStatusInvalidValueException(Exception):
    pass

class ExceptionWrapper(object):
    def __enter__(self):
        pass

    def __exit__(self, exception_type, value, traceback):
        if value is not None:
            if exception_type is _pyhailort.HailoRTStatusException:
                self._raise_indicative_status_exception(value)
            else:
                raise

    @staticmethod
    def create_exception_from_status(error_code):
        string_error_code = get_status_message(error_code)
        if string_error_code == "HAILO_UNSUPPORTED_CONTROL_PROTOCOL_VERSION":
            return InvalidProtocolVersionException("HailoRT has failed because an invalid protocol version was received from device")
        if string_error_code == "HAILO_FW_CONTROL_FAILURE":
            return HailoRTFirmwareControlFailedException("libhailort control operation failed")
        if string_error_code == "HAILO_UNSUPPORTED_OPCODE":
            return HailoRTUnsupportedOpcodeException("HailoRT has failed because an unsupported opcode was sent to device")
        if string_error_code == "HAILO_INVALID_FRAME":
            return HailoRTInvalidFrameException("An invalid frame was received")
        if string_error_code == "HAILO_TIMEOUT":
            return HailoRTTimeout("Received a timeout - hailort has failed because a timeout had occurred")
        if string_error_code == "HAILO_STREAM_ABORT":
            return HailoRTStreamAborted("Stream was aborted")

        if string_error_code == "HAILO_INVALID_OPERATION":
            return HailoRTInvalidOperationException("Invalid operation. See hailort.log for more information")
        if string_error_code == "HAILO_INVALID_ARGUMENT":
            return HailoRTInvalidArgumentException("Invalid argument. See hailort.log for more information")
        if string_error_code == "HAILO_NOT_FOUND":
            return HailoRTNotFoundException("Item not found. See hailort.log for more information")

        if string_error_code == "HAILO_INVALID_HEF":
            return HailoRTInvalidHEFException("Invalid HEF. See hailort.log for more information")
        if string_error_code == "HAILO_HEF_NOT_COMPATIBLE_WITH_DEVICE":
            return HailoRTHEFNotCompatibleWithDevice("HEF file is not compatible with device. See hailort.log for more information")

        if string_error_code == "HAILO_ETH_FAILURE":
            return HailoRTEthException("Ethernet failure. See hailort.log for more information")
        if string_error_code == "HAILO_DRIVER_OPERATION_FAILED":
            return HailoRTDriverOperationFailedException("Failure in hailort driver ioctl. run 'dmesg | grep hailo' for more information")

        if string_error_code == "HAILO_COMMUNICATION_CLOSED":
            return HailoCommunicationClosedException("Communication closed failure. Check server-client connection")

        if string_error_code == "HAILO_NETWORK_GROUP_NOT_ACTIVATED":
            return HailoRTNetworkGroupNotActivatedException("Network group is not activated")
        else:
            return HailoRTException("libhailort failed with error: {} ({})".format(error_code, string_error_code))


    def _raise_indicative_status_exception(self, libhailort_exception):
        error_code = int(libhailort_exception.args[0])
        raise self.create_exception_from_status(error_code) from libhailort_exception


def get_status_message(status_code):
    status_str = _pyhailort.get_status_message(status_code)
    if status_str == "":
        raise HailoStatusInvalidValueException("Value {} is not a valid status".format(status_code))
    return status_str


class ConfigureParams(object):

    @staticmethod
    def create_from_hef(hef, interface):
        """Create configure params from HEF. These params affects the HEF configuration into a device.

        Args:
            hef (:class:`HEF`): The HEF to create the parameters from.
            interface (:class:`HailoStreamInterface`): The stream_interface to create stream_params for.

        Returns:
            dict: The created stream params. The keys are the network_group names in the HEF. The values are default params, which can be changed.
        """
        with ExceptionWrapper():
            return hef._hef.create_configure_params(interface)

def _get_name_as_str(name):
    return name if name is not None else ""

class HEF(object):
    """Python representation of the Hailo Executable Format, which contains one or more compiled
    models.
    """

    def __init__(self, hef_source):
        """Constructor for the HEF class.

        Args:
            hef_source (str or bytes): The source from which the HEF object will be created. If the
                source type is `str`, it is treated as a path to an hef file. If the source type is
                `bytes`, it is treated as a buffer. Any other type will raise a ValueError.
        """

        with ExceptionWrapper():
            if isinstance(hef_source, str):
                self._hef = _pyhailort.Hef.create_from_file(hef_source)
                self._path = hef_source
            elif isinstance(hef_source, bytes):
                self._hef = _pyhailort.Hef.create_from_buffer(hef_source)
                self._path = None
            else: 
                raise ValueError("HEF can only be created from a file path (str) or a buffer (bytes)")
        self._sorted_output_names = {}

    def get_networks_names(self, network_group_name=None):
        """Gets the names of all networks in a specific network group.

        Args:
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: The names of the networks.
        """
        name = _get_name_as_str(network_group_name)
        with ExceptionWrapper():
            return self._hef.get_networks_names(name)

    @property
    def path(self):
        """HEF file path."""
        return self._path
    
    def get_network_group_names(self):
        """Get the names of the network groups in this HEF."""
        with ExceptionWrapper():
            return self._hef.get_network_group_names()
  
    def get_network_groups_infos(self):
        """Get information about the network groups in this HEF."""
        with ExceptionWrapper():
            return self._hef.get_network_groups_infos()

    def _get_external_resources(self, resource_name):
        # Returns a buffer of the appended external resources with this name as the resource raw-bytes.
        with ExceptionWrapper():
            return self._hef.get_external_resources(resource_name)

    def _get_external_resource_names(self):
        # Returns the names of the external resources in this HEF.
        with ExceptionWrapper():
            return self._hef.get_external_resource_names()

    def get_input_vstream_infos(self, name=None):
        """Get input vstreams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input vstreams.
        """
        name = _get_name_as_str(name)
        return self._hef.get_input_vstream_infos(name)

    def get_output_vstream_infos(self, name=None):
        """Get output vstreams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all output vstreams
        """
        name = _get_name_as_str(name)
        return self._hef.get_output_vstream_infos(name)

    def get_all_vstream_infos(self, name=None):
        """Get input and output vstreams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input and output vstreams
        """
        name = _get_name_as_str(name)
        return self._hef.get_all_vstream_infos(name)

    def get_input_stream_infos(self, name=None):
        """Get the input low-level streams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all input low-level streams.
        """
        name = _get_name_as_str(name)
        return self._hef.get_input_stream_infos(name)


    def get_output_stream_infos(self, name=None):
        """Get the output low-level streams information of a specific network group.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all output low-level streams.
        """
        name = _get_name_as_str(name)
        return self._hef.get_output_stream_infos(name)

    def get_all_stream_infos(self, name=None):
        """Get input and output streams information of a specific network group.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with all the information objects of all input and output streams
        """
        name = _get_name_as_str(name)
        return self._hef.get_all_stream_infos(name)

    def get_sorted_output_names(self, network_group_name=None):
        """Get the names of the outputs in a network group. The order of names is determined by
        the SDK. If the network group is not given, the first one is used.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]

        if network_group_name not in self._sorted_output_names:
            with ExceptionWrapper():
                self._sorted_output_names[network_group_name] = self._hef.get_sorted_output_names(network_group_name)
        return self._sorted_output_names[network_group_name]

    def bottleneck_fps(self, network_group_name=None):
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            bottleneck_fps = self._hef.get_bottleneck_fps(network_group_name)
            if bottleneck_fps == 0:
                raise HailoRTException("bottleneck_fps is zero")
            return bottleneck_fps

    def get_vstream_name_from_original_name(self, original_name, network_group_name=None):
        """Get vstream name from original layer name for a specific network group.

        Args:
            original_name (str): The original layer name.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            str: the matching vstream name for the provided original name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_vstream_name_from_original_name(original_name, network_group_name)

    def get_original_names_from_vstream_name(self, vstream_name, network_group_name=None):
        """Get original names list from vstream name for a specific network group.

        Args:
            vstream_name (str): The stream name.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: all the matching original layers names for the provided vstream name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_original_names_from_vstream_name(vstream_name, network_group_name)

    def get_vstream_names_from_stream_name(self, stream_name, network_group_name=None):
        """Get vstream names list from their underlying stream name for a specific network group.

        Args:
            stream_name (str): The underlying stream name.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: All the matching vstream names for the provided stream name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_vstream_names_from_stream_name(stream_name, network_group_name)

    def get_stream_names_from_vstream_name(self, vstream_name, network_group_name=None):
        """Get stream name from vstream name for a specific network group.

        Args:
            vstream_name (str): The name of the vstreams.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: All the underlying streams names for the provided vstream name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_stream_names_from_vstream_name(vstream_name, network_group_name)


class ConfiguredNetwork(object):
    """Represents a network group loaded to the device."""

    def __init__(self, configured_network):
        self._configured_network = configured_network
        self._input_vstreams_holders = []
        self._output_vstreams_holders = []

    def get_networks_names(self):
        return self._configured_network.get_networks_names()

    def activate(self, network_group_params=None):
        """Activate this network group in order to infer data through it.

        Args:
            network_group_params (:obj:`hailo_platform.pyhailort._pyhailort.ActivateNetworkGroupParams`, optional):
                Network group activation params. If not given, default params will be applied,

        Returns:
            :class:`ActivatedNetworkContextManager`: Context manager that returns the activated
            network group.

        Note:
            Usage of `activate` when scheduler enabled is deprecated. On this case, this function will return None and print deprecation warning.
        """
        if self._configured_network.is_scheduled():
            default_logger().warning("Calls to `activate()` when working with scheduler are deprecated! On future versions this call will raise an error.")
            return EmptyContextManager()

        network_group_params = network_group_params or self.create_params()
        with ExceptionWrapper():
            return ActivatedNetworkContextManager(self,
                self._configured_network.activate(network_group_params))

    def wait_for_activation(self, timeout_ms=None):
        """Block until activated, or until ``timeout_ms`` is passed.

        Args:
            timeout_ms (int, optional): Timeout value in milliseconds to wait for activation.
                Defaults to ``HAILO_INFINITE``.

        Raises:
            :class:`HailoRTTimeout`: In case of timeout.
        """
        MAX_INT = 0x7fffffff
        with ExceptionWrapper():
            if timeout_ms is None:
                timeout_ms = MAX_INT
            return self._configured_network.wait_for_activation(timeout_ms)

    @staticmethod
    def create_params():
        """Create activation params for network_group.

        Returns:
            :obj:`hailo_platform.pyhailort._pyhailort.ActivateNetworkGroupParams`.
        """
        return _pyhailort.ActivateNetworkGroupParams.default()

    @property
    def name(self):
        return self._configured_network.get_name()

    def get_output_shapes(self):
        name_to_shape = {vstream_info.name : vstream_info.shape for vstream_info in self.get_output_vstream_infos()}
        results = []
        for name in self.get_sorted_output_names():
            results.append(name_to_shape[name])
        return tuple(results)

    def get_sorted_output_names(self):
        return self._configured_network.get_sorted_output_names()

    def get_input_vstream_infos(self, network_name=None):
        """Get input vstreams information.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input vstreams
        """

        name = network_name if network_name is not None else ""
        return self._configured_network.get_input_vstream_infos(name)

    def get_output_vstream_infos(self, network_name=None):
        """Get output vstreams information.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all output vstreams
        """

        name = network_name if network_name is not None else ""
        return self._configured_network.get_output_vstream_infos(name)

    def get_all_vstream_infos(self, network_name=None):
        """Get input and output vstreams information.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input and output vstreams
        """

        name = network_name if network_name is not None else ""
        return self._configured_network.get_all_vstream_infos(name)

    def get_input_stream_infos(self, network_name=None):
        """Get the input low-level streams information of a specific network group.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all input low-level streams.
        """

        name = network_name if network_name is not None else ""
        return self._configured_network.get_input_stream_infos(name)

    def get_output_stream_infos(self, network_name=None):
        """Get the output low-level streams information of a specific network group.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all output low-level streams.
        """

        name = network_name if network_name is not None else ""
        return self._configured_network.get_output_stream_infos(name)

    def get_all_stream_infos(self, network_name=None):
        """Get input and output streams information of a specific network group.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with all the information objects of all input and output streams
        """

        name = network_name if network_name is not None else ""
        return self._configured_network.get_all_stream_infos(name)

    def _create_input_vstreams(self, input_vstreams_params):
        input_vstreams_holder = self._configured_network.InputVStreams(input_vstreams_params)
        self._input_vstreams_holders.append(input_vstreams_holder)
        return input_vstreams_holder

    def _create_output_vstreams(self, output_vstreams_params):
        output_vstreams_holder = self._configured_network.OutputVStreams(output_vstreams_params)
        self._output_vstreams_holders.append(output_vstreams_holder)
        return output_vstreams_holder

    def get_stream_names_from_vstream_name(self, vstream_name):
        """Get stream name from vstream name for a specific network group.

        Args:
            vstream_name (str): The name of the vstreams.

        Returns:
            list of str: All the underlying streams names for the provided vstream name.
        """
        with ExceptionWrapper():
            return self._configured_network.get_stream_names_from_vstream_name(vstream_name)

    def get_vstream_names_from_stream_name(self, stream_name):
        """Get vstream names list from their underlying stream name for a specific network group.

        Args:
            stream_name (str): The underlying stream name.

        Returns:
            list of str: All the matching vstream names for the provided stream name.
        """
        with ExceptionWrapper():
            return self._configured_network.get_vstream_names_from_stream_name(stream_name)

    def set_scheduler_timeout(self, timeout_ms, network_name=None):
        """Sets the maximum time period that may pass before receiving run time from the scheduler.
            This will occur providing at least one send request has been sent, there is no minimum requirement for send
            requests, (e.g. threshold - see :func:`ConfiguredNetwork.set_scheduler_threshold`).

        Args:
            timeout_ms (int): Timeout in milliseconds.
        """
        name = network_name if network_name is not None else ""
        return self._configured_network.set_scheduler_timeout(timeout_ms, name)

    def set_scheduler_threshold(self, threshold):
        """Sets the scheduler threshold.
            This threshold sets the minimum number of send requests required before the network is considered ready to get run time from the scheduler.
            If at least one send request has been sent, but the threshold is not reached within a set time period (e.g. timeout - see :func:`ConfiguredNetwork.set_scheduler_timeout`),
            the scheduler will consider the network ready regardless.

        Args:
            threshold (int): Threshold in number of frames.
        """
        return self._configured_network.set_scheduler_threshold(threshold)

    def set_scheduler_priority(self, priority):
        """Sets the priority of the network.
            When the model scheduler will choose the next network, networks with higher priority will be prioritized in the selection.
            Larger number represents higher priority.

        Args:
            priority (int): Priority as a number between HAILO_SCHEDULER_PRIORITY_MIN - HAILO_SCHEDULER_PRIORITY_MAX.
        """
        return self._configured_network.set_scheduler_priority(priority)

    def init_cache(self, read_offset):
        return self._configured_network.init_cache(read_offset)

    def update_cache_offset(self, offset_delta_entries):
        return self._configured_network.update_cache_offset(offset_delta_entries)

    def get_cache_ids(self) -> List[int]:
        return self._configured_network.get_cache_ids()

    def read_cache_buffer(self, cache_id: int) -> bytes:
        return self._configured_network.read_cache_buffer(cache_id)

    def write_cache_buffer(self, cache_id: int, buffer: bytes):
        return self._configured_network.write_cache_buffer(cache_id, buffer)

class EmptyContextManager(object):
    """An empty context manager that returns instead of activated network group when scheduler is enabled`."""

    def __init__(self):
        pass

    def __enter__(self):
        pass

    def __exit__(self, *args):
        pass


class ActivatedNetworkContextManager(object):
    """A context manager that returns the activated network group upon enter."""

    def __init__(self, configured_network, activated_network):
        self._configured_network = configured_network
        self._activated_network = activated_network

    def __enter__(self):
        with ExceptionWrapper():
            activated_network_group = ActivatedNetwork(self._configured_network, self._activated_network.__enter__())
        return activated_network_group
    
    def __exit__(self, *args):
        self._activated_network.__exit__(*args)


class ActivatedNetwork(object):
    """The network group that is currently activated for inference."""

    def __init__(self, configured_network, activated_network):
        self._configured_network = configured_network
        self._activated_network = activated_network
        self._last_number_of_invalid_frames_read = 0

    @property
    def name(self):
        return self._configured_network.name

    def get_number_of_invalid_frames(self, clear=True):
        """Returns number of invalid frames.

        Args:
            clear (bool): If set, the returned value will be the number of invalid frames read since the last call to this function.

        Returns:
            int: Number of invalid frames.
        """
        total_invalid_frames_count = self._activated_network.get_invalid_frames_count()
        if clear:
            value = total_invalid_frames_count - self._last_number_of_invalid_frames_read
        self._last_number_of_invalid_frames_read = total_invalid_frames_count
        return value if clear else total_invalid_frames_count

    def validate_all_frames_are_valid(self):
        """Validates that all of the frames so far are valid (no invalid frames)."""
        number_of_invalid_frames = self.get_number_of_invalid_frames()
        if number_of_invalid_frames != 0:
            raise HailoRTException("There are {} invalid frames.".format(number_of_invalid_frames))

    def get_sorted_output_names(self):
        return self._configured_network.get_sorted_output_names()

    def _get_intermediate_buffer(self, src_context_index, src_stream_index):
        with ExceptionWrapper():
            return self._activated_network.get_intermediate_buffer(src_context_index, src_stream_index)


class InferVStreams(object):
    """Pipeline that allows to call blocking inference, to be used as a context manager."""

    def __init__(self, configured_net_group, input_vstreams_params, output_vstreams_params,
        tf_nms_format=False):
        """Constructor for the InferVStreams class.

        Args:
            configured_net_group (:class:`ConfiguredNetwork`): The configured network group for
                which the pipeline is created.
            input_vstreams_params (dict from str to :class:`InputVStreamParams`): Params for the
                input vstreams in the pipeline. Only members of this dict will take part in the
                inference.
            output_vstreams_params (dict from str to :class:`OutputVStreamParams`): Params for the
                output vstreams in the pipeline. Only members of this dict will take part in the
                inference.
            tf_nms_format (bool, optional): indicates whether the returned nms outputs should be in
                Hailo format or TensorFlow format. Default is False (using Hailo format).

                * Hailo format -- list of :obj:`numpy.ndarray`. Each element represents the
                  detections (bboxes) for the class, and its shape is
                  ``[number_of_detections, BBOX_PARAMS]``
                * TensorFlow format -- :obj:`numpy.ndarray` of shape
                  ``[class_count, BBOX_PARAMS, detections_count]`` padded with empty bboxes.
        """

        self._configured_net_group = configured_net_group
        self._net_group_name = configured_net_group.name
        self._input_vstreams_params = input_vstreams_params
        self._output_vstreams_params = output_vstreams_params
        self._tf_nms_format = tf_nms_format
        self._total_time = None
        self._hw_time = None
        self._network_name_to_outputs = InferVStreams._get_network_to_outputs_mapping(configured_net_group)
        self._input_name_to_network_name = InferVStreams._get_input_name_to_network_mapping(configured_net_group)

    @staticmethod
    def _get_input_name_to_network_mapping(configured_net_group):
        input_name_to_network_mapping = {}
        for network_name in configured_net_group.get_networks_names():
            for input_vstream_info in configured_net_group.get_input_vstream_infos(network_name):
                input_name_to_network_mapping[input_vstream_info.name] = network_name
        return input_name_to_network_mapping

    @staticmethod
    def _get_network_to_outputs_mapping(configured_net_group):
        network_to_outputs_mapping = {}
        for network_name in configured_net_group.get_networks_names():
            network_to_outputs_mapping[network_name] = set()
            for output_vstream_info in configured_net_group.get_output_vstream_infos(network_name):
                network_to_outputs_mapping[network_name].add(output_vstream_info.name)
        return network_to_outputs_mapping

    def _make_output_buffers_and_infos(self, input_data, batch_size):
        output_buffers = {}
        output_buffers_info = {}
        already_seen_networks = set()
        for input_name in input_data.keys():
            network_name = self._input_name_to_network_name[input_name]
            if (network_name not in already_seen_networks) :
                already_seen_networks.add(network_name)
                output_vstream_infos = self._configured_net_group.get_output_vstream_infos()
                for output_name in self._network_name_to_outputs[network_name]:
                    output_buffers_info[output_name] = OutputLayerUtils(output_vstream_infos, output_name, self._infer_pipeline,
                        self._net_group_name)
                    output_tensor_info = output_buffers_info[output_name].output_tensor_info
                    shape, dtype = output_tensor_info
                    if (output_buffers_info[output_name].output_order == FormatOrder.HAILO_NMS_WITH_BYTE_MASK):
                        # Note: In python bindings the output data gets converted to py::array with dtype=dtype.
                        #   In `HAILO_NMS_WITH_BYTE_MASK` we would like to get the data as uint8 and convert it by it's format.
                        #   Therefore we need to get it as uint8 instead of float32
                        dtype = numpy.uint8
                    output_buffers[output_name] = numpy.empty([batch_size] + list(shape), dtype=dtype)
        return output_buffers, output_buffers_info

    def __enter__(self):
        self._infer_pipeline = _pyhailort.InferVStreams(self._configured_net_group._configured_network,
            self._input_vstreams_params, self._output_vstreams_params)
        return self
    
    def infer(self, input_data):
        """Run inference on the hardware device.

        Args:
            input_data (dict of :obj:`numpy.ndarray`): Where the key is the name of the input_layer,
                and the value is the data to run inference on.

        Returns:
            dict: Output tensors of all output layers. The keys are outputs names and the values
            are output data tensors as :obj:`numpy.ndarray` (or list of :obj:`numpy.ndarray` in case of nms output and tf_nms_format=False).
        """

        time_before_infer_calcs = time.perf_counter()
        if not isinstance(input_data, dict):
            input_vstream_infos = self._configured_net_group.get_input_vstream_infos()
            if len(input_vstream_infos) != 1:
                raise Exception("when there is more than one input, the input_data should be of type dict,"
                                             " mapping between each input_name, and his input_data tensor. number of inputs: {}".format(len(input_vstream_infos)))
            input_data = {input_vstream_infos[0].name : input_data}

        batch_size = InferVStreams._get_number_of_frames(input_data)
        output_buffers, output_buffers_info = self._make_output_buffers_and_infos(input_data, batch_size)

        for input_layer_name in input_data:
            # TODO: Remove cast after tests are updated and are working
            self._cast_input_data_if_needed(input_layer_name, input_data)
            self._validate_input_data_format_type(input_layer_name, input_data)
            self._make_c_contiguous_if_needed(input_layer_name, input_data)

        with ExceptionWrapper():
            time_before_infer = time.perf_counter()
            self._infer_pipeline.infer(input_data, output_buffers, batch_size)
            self._hw_time = time.perf_counter() - time_before_infer

        for name, result_array in output_buffers.items():
            # TODO: HRT-11726 - Combine Pyhailort NMS and NMS_WITH_BYTE_MASK decoding function
            if output_buffers_info[name].output_order == FormatOrder.HAILO_NMS_WITH_BYTE_MASK:
                nms_shape = output_buffers_info[name].vstream_info.nms_shape
                output_dtype = output_buffers_info[name].output_dtype
                input_vstream_infos = self._configured_net_group.get_input_vstream_infos()
                if len(input_vstream_infos) != 1:
                    raise Exception("Output format HAILO_NMS_WITH_BYTE_MASK should have 1 input. Number of inputs: {}".format(len(input_vstream_infos)))
                input_height = input_vstream_infos[0].shape[0]
                input_width = input_vstream_infos[0].shape[1]
                output_buffers[name] = HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_format(result_array,
                    nms_shape.number_of_classes, batch_size, input_height, input_width,
                    nms_shape.max_bboxes_per_class, output_dtype, self._tf_nms_format)
                continue

            is_nms = output_buffers_info[name].is_nms
            if not is_nms:
                continue
            nms_shape = output_buffers_info[name].vstream_info.nms_shape
            if self._tf_nms_format:
                shape = [batch_size] + output_buffers_info[name].tf_nms_fomrat_shape
                output_dtype = output_buffers_info[name].output_dtype
                quantized_empty_bbox = output_buffers_info[name].quantized_empty_bbox
                flat_result_array = result_array.reshape(-1)
                output_buffers[name] = HailoRTTransformUtils.output_raw_buffer_to_nms_tf_format(flat_result_array, shape,
                    output_dtype, quantized_empty_bbox)
            else:
                output_buffers[name] = HailoRTTransformUtils.output_raw_buffer_to_nms_format(result_array, nms_shape.number_of_classes)

        self._total_time = time.perf_counter() - time_before_infer_calcs
        return output_buffers

    def get_hw_time(self):
        """Get the hardware device operation time it took to run inference over the last batch.

        Returns:
            float: Time in seconds.
        """
        return self._hw_time

    def get_total_time(self):
        """Get the total time it took to run inference over the last batch.

        Returns:
            float: Time in seconds.
        """
        return self._total_time

    def _cast_input_data_if_needed(self, input_layer_name, input_data):
        input_dtype = input_data[input_layer_name].dtype
        with ExceptionWrapper():
            input_expected_dtype = self._infer_pipeline.get_host_dtype(input_layer_name)
        if input_dtype != input_expected_dtype:

            default_logger().warning("Given input data dtype ({}) is different than inferred dtype ({}). "
                "conversion for every frame will reduce performance".format(input_dtype,
                    input_expected_dtype))
            input_data[input_layer_name] = input_data[input_layer_name].astype(input_expected_dtype)
    
    def _validate_input_data_format_type(self, input_layer_name, input_data):
        if input_layer_name not in self._input_vstreams_params:
            return

        input_data_format = self._input_vstreams_params[input_layer_name].user_buffer_format
        input_expected_item_size = _pyhailort.get_format_data_bytes(input_data_format)
        input_item_size = input_data[input_layer_name].dtype.itemsize

        # TODO: Add distinction between float32 and int32 and others
        if input_item_size != input_expected_item_size:
            raise HailoRTException("{} numpy array item size is {}, not {}".format(input_layer_name,
                input_item_size, input_expected_item_size))

    @staticmethod
    def _get_number_of_frames(input_data):
        # Checks that all the batch-sizes of the input_data are equals for all input layers
        if len(input_data) == 0:
            raise ValueError("Input_data can't be empty")
        batch_size_of_first_input = list(input_data.values())[0].shape[0]
        for name, input_data_tensor in input_data.items():
            if input_data_tensor.shape[0] != batch_size_of_first_input:
                raise ValueError(
                    "The number of frames on all input_tensors should be equal! different sizes detected: {} != {}".format(
                        batch_size_of_first_input, input_data_tensor.shape[0]))
        return batch_size_of_first_input

    def _make_c_contiguous_if_needed(self, input_layer_name, input_data):
        if not input_data[input_layer_name].flags.c_contiguous:
            default_logger().warning("Converting {} numpy array to be C_CONTIGUOUS".format(
                input_layer_name))
            input_data[input_layer_name] = numpy.asarray(input_data[input_layer_name], order='C')

    def set_nms_score_threshold(self, threshold):
        """Set NMS score threshold, used for filtering out candidates. Any box with score<TH is suppressed.

        Args:
            threshold (float): NMS score threshold to set.

        Note:
            This function will fail in cases where there is no output with NMS operations on the CPU.
        """
        return self._infer_pipeline.set_nms_score_threshold(threshold)

    def set_nms_iou_threshold(self, threshold):
        """Set NMS intersection over union overlap Threshold,
            used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.

        Args:
            threshold (float): NMS IoU threshold to set.

        Note:
            This function will fail in cases where there is no output with NMS operations on the CPU.
        """
        return self._infer_pipeline.set_nms_iou_threshold(threshold)

    def set_nms_max_proposals_per_class(self, max_proposals_per_class):
        """Set a limit for the maximum number of boxes per class.

        Args:
            max_proposals_per_class (int): NMS max proposals per class to set.

        Note:
            This function must be called before starting inference!
            This function will fail in cases where there is no output with NMS operations on the CPU.
        """
        return self._infer_pipeline.set_nms_max_proposals_per_class(max_proposals_per_class)

    def set_nms_max_accumulated_mask_size(self, max_accumulated_mask_size):
        """Set maximum accumulated mask size for all the detections in a frame.
            Used in order to change the output buffer frame size,
            in cases where the output buffer is too small for all the segmentation detections.

        Args:
            max_accumulated_mask_size (int): NMS max accumulated mask size.

        Note:
            This function must be called before starting inference!
            This function will fail in cases where there is no output with NMS operations on the CPU.
        """
        return self._infer_pipeline.set_nms_max_accumulated_mask_size(max_accumulated_mask_size)

    def __exit__(self, *args):
        self._infer_pipeline.release()
        return False

class Detection(object):
    """Represents a detection information"""

    def __init__(self, detection):
        self._y_min = detection.y_min
        self._x_min = detection.x_min
        self._y_max = detection.y_max
        self._x_max = detection.x_max
        self._score = detection.score
        self._class_id = detection.class_id

    @property
    def y_min(self):
        """Get detection's box y_min coordinate"""
        return self._y_min

    @property
    def x_min(self):
        """Get detection's box x_min coordinate"""
        return self._x_min

    @property
    def y_max(self):
        """Get detection's box y_max coordinate"""
        return self._y_max

    @property
    def x_max(self):
        """Get detection's box x_max coordinate"""
        return self._x_max

    @property
    def score(self):
        """Get detection's score"""
        return self._score

    @property
    def class_id(self):
        """Get detection's class_id"""
        return self._class_id


class DetectionWithByteMask(object):
    """Represents a detection with byte mask information"""

    def __init__(self, detection):
        self._y_min = detection.box.y_min
        self._x_min = detection.box.x_min
        self._y_max = detection.box.y_max
        self._x_max = detection.box.x_max
        self._score = detection.score
        self._class_id = detection.class_id
        self._mask = detection.mask()

    @property
    def y_min(self):
        """Get detection's box y_min coordinate"""
        return self._y_min

    @property
    def x_min(self):
        """Get detection's box x_min coordinate"""
        return self._x_min

    @property
    def y_max(self):
        """Get detection's box y_max coordinate"""
        return self._y_max

    @property
    def x_max(self):
        """Get detection's box x_max coordinate"""
        return self._x_max

    @property
    def score(self):
        """Get detection's score"""
        return self._score

    @property
    def class_id(self):
        """Get detection's class_id"""
        return self._class_id

    @property
    def mask(self):
        """Byte Mask:
        The mask is a binary mask that defines a region of interest (ROI) of the image.
        Mask pixel values of 1 indicate image pixels that belong to the ROI.
        Mask pixel values of 0 indicate image pixels that are part of the background.

        The size of the mask is the size of the box, in the original input image's dimensions.
        Mask width = ceil((x_max - x_min) * image_width)
        Mask height = ceil((y_max - y_min) * image_height)
        First pixel represents the pixel (x_min * image_width, y_min * image_height) in the original input image.
        """
        return self._mask

class HailoRTTransformUtils(object):
    @staticmethod
    def get_dtype(data_bytes):
        """Get data type from the number of bytes."""
        if data_bytes == 1:
            return numpy.uint8
        elif data_bytes == 2:
            return numpy.uint16
        elif data_bytes == 4:
            return numpy.float32
        raise HailoRTException("unsupported data bytes value")

    @staticmethod
    def dequantize_output_buffer(src_buffer, dst_buffer, elements_count, quant_info):
        """De-quantize the data in input buffer `src_buffer` and output it to the buffer `dst_buffer`

        Args:
            src_buffer (:obj:`numpy.ndarray`): The input buffer containing the data to be de-quantized.
                The buffer's data type is the source data type.
            dst_buffer (:obj:`numpy.ndarray`): The buffer that will contain the de-quantized data.
                The buffer's data type is the destination data type.
            elements_count (int): The number of elements to de-quantize. This number must not exceed 'src_buffer' or 'dst_buffer' sizes.
            quant_info (:class:`~hailo_platform.pyhailort.pyhailort.QuantInfo`): The quantization info.
        """
        with ExceptionWrapper():
            src_format_type = HailoRTTransformUtils._get_format_type(src_buffer.dtype)
            dst_format_type = HailoRTTransformUtils._get_format_type(dst_buffer.dtype)
            if not _pyhailort.is_qp_valid(quant_info):
                raise HailoRTInvalidOperationException("quant_info is invalid as the model was compiled with multiple quant_infos. "
                                                       "Please compile again or provide a list of quant_infos.")
            _pyhailort.dequantize_output_buffer(src_buffer, dst_buffer, src_format_type, dst_format_type, elements_count, quant_info)

    @staticmethod
    def dequantize_output_buffer_in_place(raw_buffer, dst_dtype, elements_count, quant_info):
        """De-quantize the output buffer `raw_buffer` to data type `dst_dtype`.

        Args:
            raw_buffer (:obj:`numpy.ndarray`): The output buffer to be de-quantized. The buffer's data type is the source data type.
            dst_dtype (:obj:`numpy.dtype`): The data type to de-quantize `raw_buffer` to.
            elements_count (int): The number of elements to de-quantize. This number must not exceed 'raw_buffer' size.
            quant_info (:class:`~hailo_platform.pyhailort.pyhailort.QuantInfo`): The quantization info.
        """
        with ExceptionWrapper():
            src_format_type = HailoRTTransformUtils._get_format_type(raw_buffer.dtype)
            dst_format_type = HailoRTTransformUtils._get_format_type(dst_dtype)
            if not _pyhailort.is_qp_valid(quant_info):
                raise HailoRTInvalidOperationException("quant_info is invalid as the model was compiled with multiple quant_infos. "
                                                       "Please compile again or provide a list of quant_infos.")
            _pyhailort.dequantize_output_buffer_in_place(raw_buffer, src_format_type, dst_format_type, elements_count, quant_info)

    @staticmethod
    def quantize_input_buffer(src_buffer, dst_buffer, elements_count, quant_info):
        """Quantize the data in input buffer `src_buffer` and output it to the buffer `dst_buffer`

        Args:
            src_buffer (:obj:`numpy.ndarray`): The input buffer containing the data to be quantized.
                The buffer's data type is the source data type.
            dst_buffer (:obj:`numpy.ndarray`): The buffer that will contain the quantized data.
                The buffer's data type is the destination data type.
            elements_count (int): The number of elements to quantize. This number must not exceed 'src_buffer' or 'dst_buffer' sizes.
            quant_info (:class:`~hailo_platform.pyhailort.pyhailort.QuantInfo`): The quantization info.
        """
        with ExceptionWrapper():
            src_format_type = HailoRTTransformUtils._get_format_type(src_buffer.dtype)
            dst_format_type = HailoRTTransformUtils._get_format_type(dst_buffer.dtype)
            if not _pyhailort.is_qp_valid(quant_info):
                raise HailoRTInvalidOperationException("quant_info is invalid as the model was compiled with multiple quant_infos. "
                                                       "Please compile again or provide a list of quant_infos.")
            _pyhailort.quantize_input_buffer(src_buffer, dst_buffer, src_format_type, dst_format_type, elements_count, quant_info)

    @staticmethod
    def output_raw_buffer_to_nms_tf_format(raw_output_buffer, shape, dtype, quantized_empty_bbox):
        offset = 0
        # We create the tf_format buffer with reversed width/features for preformance optimization
        converted_output_buffer = numpy.empty([shape[0], shape[1], shape[3], shape[2]], dtype=dtype)
        for frame in range(converted_output_buffer.shape[0]):
            offset = frame * converted_output_buffer.shape[1] * (converted_output_buffer.shape[2] * converted_output_buffer.shape[3] + 1)
            HailoRTTransformUtils.output_raw_buffer_to_nms_tf_format_single_frame(raw_output_buffer, converted_output_buffer[frame],
                converted_output_buffer.shape[1], converted_output_buffer.shape[2], quantized_empty_bbox, offset)
        converted_output_buffer = numpy.swapaxes(converted_output_buffer, 2, 3)
        return converted_output_buffer

    @staticmethod
    def output_raw_buffer_to_nms_tf_format_single_frame(raw_output_buffer, converted_output_frame, number_of_classes,
        max_bboxes_per_class, quantized_empty_bbox, offset=0):
        for class_i in range(number_of_classes):
            class_bboxes_amount = int(raw_output_buffer[offset])
            offset += 1
            if 0 != class_bboxes_amount:
                converted_output_frame[class_i][ : class_bboxes_amount][:] = raw_output_buffer[offset : offset + (BBOX_PARAMS * class_bboxes_amount)].reshape(class_bboxes_amount, BBOX_PARAMS)
                offset += BBOX_PARAMS * class_bboxes_amount
            converted_output_frame[class_i][class_bboxes_amount : max_bboxes_per_class][:] = quantized_empty_bbox

    @staticmethod
    def output_raw_buffer_to_nms_format(raw_output_buffer, number_of_classes):
        converted_output_buffer = []
        for frame in raw_output_buffer:
            converted_output_buffer.append(HailoRTTransformUtils.output_raw_buffer_to_nms_format_single_frame(frame, number_of_classes))
        return converted_output_buffer

    @staticmethod
    def output_raw_buffer_to_nms_format_single_frame(raw_output_buffer, number_of_classes, offset=0):
        converted_output_frame = []
        for class_i in range(number_of_classes):
            class_bboxes_amount = int(raw_output_buffer[offset])
            offset += 1
            if class_bboxes_amount == 0:
                converted_output_frame.append(numpy.empty([0, BBOX_PARAMS]))
            else:
                converted_output_frame.append(raw_output_buffer[offset : offset + (BBOX_PARAMS * class_bboxes_amount)].reshape(
                    class_bboxes_amount, BBOX_PARAMS))
                offset += BBOX_PARAMS * class_bboxes_amount
        return converted_output_frame

    @staticmethod
    def _output_raw_buffer_to_nms_by_score_format_single_frame(raw_output_buffer):
        detections = _pyhailort.convert_nms_by_score_buffer_to_detections(raw_output_buffer)
        converted_output_frame = []
        for detection in detections:
            converted_output_frame.append(Detection(detection))

        return converted_output_frame

    @staticmethod
    def _output_raw_buffer_to_nms_with_byte_mask_format(raw_output_buffer, number_of_classes, batch_size, image_height, image_width,
            max_bboxes_per_class, output_dtype, is_tf_format=False):
        if is_tf_format:
            return HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_tf_format(raw_output_buffer, number_of_classes,
                batch_size, image_height, image_width, max_bboxes_per_class, output_dtype)
        else:
            return HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_hailo_format(raw_output_buffer)

    @staticmethod
    def _output_raw_buffer_to_nms_with_byte_mask_hailo_format(raw_output_buffer):
        converted_output_buffer = []
        for frame in raw_output_buffer:
            converted_output_buffer.append(
                HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_hailo_format_single_frame(frame))
        return converted_output_buffer

    @staticmethod
    def _output_raw_buffer_to_nms_with_byte_mask_hailo_format_single_frame(raw_output_buffer):
        detections = _pyhailort.convert_nms_with_byte_mask_buffer_to_detections(raw_output_buffer)
        converted_output_frame = []
        for detection in detections:
            converted_output_frame.append(DetectionWithByteMask(detection))

        return converted_output_frame

    @staticmethod
    def _output_raw_buffer_to_nms_with_byte_mask_tf_format(raw_output_buffer, number_of_classes, batch_size, image_height, image_width,
            max_bboxes_per_class, output_dtype):

        BBOX_WITH_MASK_AXIS = 2
        CLASSES_AXIS = 1

        # We create the tf_format buffer with reversed max_bboxes_per_class/features for performance optimization
        converted_output_buffer = numpy.empty([batch_size, max_bboxes_per_class, (image_height * image_width + BBOX_WITH_MASK_PARAMS)], dtype=output_dtype)

        for frame_idx in range(len(raw_output_buffer)):
            HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_tf_format_single_frame(
                raw_output_buffer[frame_idx], converted_output_buffer[frame_idx], number_of_classes, max_bboxes_per_class,
                image_height, image_width)
        converted_output_buffer = numpy.moveaxis(converted_output_buffer, CLASSES_AXIS, BBOX_WITH_MASK_AXIS)
        converted_output_buffer = numpy.expand_dims(converted_output_buffer, 1)
        return converted_output_buffer

    @staticmethod
    def _output_raw_buffer_to_nms_with_byte_mask_tf_format_single_frame(raw_output_buffer, converted_output_frame, number_of_classes,
        max_boxes, image_height, image_width):

        detections = _pyhailort.convert_nms_with_byte_mask_buffer_to_detections(raw_output_buffer)
        bbox_idx = 0
        for detection in detections:
            if (bbox_idx >= max_boxes):
                return
            bbox = numpy.array([detection.box.y_min, detection.box.x_min, detection.box.y_max, detection.box.x_max,
                    detection.score, detection.class_id])
            bbox_mask = detection.mask()

            y_min = numpy.ceil(bbox[0] * image_height)
            x_min = numpy.ceil(bbox[1] * image_width)
            bbox_width = numpy.ceil((bbox[3] - bbox[1]) * image_width)
            resized_mask = numpy.zeros(image_height*image_width, dtype="uint8")

            for i in range(bbox_mask.size):
                if (bbox_mask[i] == 1):
                    x = int(x_min + (i % bbox_width))
                    y = int(y_min + (i // bbox_width))
                    if (x >= image_width):
                        x = image_width - 1
                    if ( y >= image_height):
                        y = image_height - 1
                    idx = (image_width * y) + x
                    resized_mask[idx] = 1

            bbox_with_mask = numpy.append(bbox, resized_mask)
            converted_output_frame[bbox_idx] = bbox_with_mask
            bbox_idx += 1


    @staticmethod
    def _get_format_type(dtype):
        if dtype == numpy.uint8:
            return FormatType.UINT8
        elif dtype == numpy.uint16:
            return FormatType.UINT16
        elif dtype == numpy.float32:
            return FormatType.FLOAT32
        raise HailoRTException("unsupported data type {}".format(dtype))


class PcieDeviceInfo(_pyhailort.PcieDeviceInfo):
    """Represents pcie device info, includeing domain, bus, device and function.
    """

    BOARD_LOCATION_HELP_STRING = 'Board location in the format of the command: "lspci -d 1e60: | cut -d\' \' -f1" ([<domain>]:<bus>:<device>.<func>). If not specified the first board is taken.'

    def __init__(self, bus, device, func, domain=None):
        super(PcieDeviceInfo, self).__init__()
        self.bus = bus
        self.device = device
        self.func = func
        if domain is None:
            self.domain = PCIE_ANY_DOMAIN
        else:
            self.domain = domain

    def __eq__(self, other):
        return (self.domain, self.bus, self.device, self.func) == (other.domain, other.bus, other.device, other.func)

    def __str__(self):
        with ExceptionWrapper():
            return super().__str__()

    def __repr__(self):
        return 'PcieDeviceInfo({})'.format(str(self))

    @classmethod
    def from_string(cls, board_location_str):
        """Parse pcie device info BDF from string. The format is [<domain>]:<bus>:<device>.<func>"""
        with ExceptionWrapper():
            device_info = _pyhailort.PcieDeviceInfo._parse(board_location_str)
            return PcieDeviceInfo(device_info.bus, device_info.device, device_info.func, device_info.domain)

    @classmethod
    def argument_type(cls, board_location_str):
        """PcieDeviceInfo Argument type for argparse parsers"""
        try:
            return cls.from_string(board_location_str)
        except HailoRTException:
            raise ArgumentTypeError('Invalid device info string, format is [<domain>]:<bus>:<device>.<func>')

# TODO: HRT-10427 - Remove
class InternalPcieDevice(object):
    def __init__(self, device_info=None):
        self.device = None
        if device_info is None:
            device_info = InternalPcieDevice.scan_devices()[0]
        self._device_info = device_info
        with ExceptionWrapper():
            self.device = _pyhailort.Device.create_pcie(self._device_info)

    def __del__(self):
        self.release()

    def release(self):
        if self.device is None:
            return
        with ExceptionWrapper():
            self.device.release()
            self.device = None

    # TODO: HRT-10427 - Move to a static method in pyhailort_internal when InternalPcieDevice removed
    @staticmethod
    def scan_devices():
        with ExceptionWrapper():
            pcie_scanner = _pyhailort.PcieScan()
            return [PcieDeviceInfo(dev_info.bus, dev_info.device, dev_info.func, dev_info.domain)
                for dev_info in pcie_scanner.scan_devices()]


class HailoPowerMeasurementUtils(object):
    @staticmethod
    def return_real_sampling_period(sampling_period):
        """Get a sampling period from the enum."""
        SamplingPeriodDictionary = dict([
                (SamplingPeriod.PERIOD_140us, 140),
                (SamplingPeriod.PERIOD_204us, 204),
                (SamplingPeriod.PERIOD_332us, 332),
                (SamplingPeriod.PERIOD_588us, 588),
                (SamplingPeriod.PERIOD_1100us, 1100),
                (SamplingPeriod.PERIOD_2116us, 2116),
                (SamplingPeriod.PERIOD_4156us, 4156),
                (SamplingPeriod.PERIOD_8244us, 8244),
        ])
        return SamplingPeriodDictionary[sampling_period]

    @staticmethod
    def return_real_averaging_factor(averaging_factor):
        """Get an averaging factor from the enum."""
        AveragingFactorDictionary = dict([
                (AveragingFactor.AVERAGE_1, 1),
                (AveragingFactor.AVERAGE_4, 4),
                (AveragingFactor.AVERAGE_16, 16),
                (AveragingFactor.AVERAGE_64, 64),
                (AveragingFactor.AVERAGE_128, 128),
                (AveragingFactor.AVERAGE_256, 256),
                (AveragingFactor.AVERAGE_512, 512),
                (AveragingFactor.AVERAGE_1024, 1024),
        ])
        return AveragingFactorDictionary[averaging_factor]

class HailoPowerMode(_pyhailort.PowerMode):
    pass

class HailoStreamInterface(_pyhailort.StreamInterface):
    pass

class HailoStreamDirection(_pyhailort.StreamDirection):
    pass

class HailoCpuId(_pyhailort.CpuId):
    pass

class HailoFormatFlags(_pyhailort.FormatFlags):
    pass

SUPPORTED_PROTOCOL_VERSION = 2
SUPPORTED_FW_MAJOR = 5
SUPPORTED_FW_MINOR = 1
SUPPORTED_FW_REVISION = 1

MEGA_MULTIPLIER = 1000.0 * 1000.0


class DeviceArchitectureTypes(IntEnum):
    HAILO8_A0 = 0
    HAILO8 = 1
    HAILO8L = 2
    HAILO15H = 3
    HAILO15L = 4
    HAILO15M = 5
    HAILO10H = 6

    def __str__(self):
        return self.name

class BoardInformation(object):
    def __init__(self, protocol_version, fw_version_major, fw_version_minor, fw_version_revision,
                 logger_version, board_name, is_release, extended_context_switch_buffer, device_architecture,
                 serial_number, part_number, product_name):
        self.protocol_version = protocol_version
        self.firmware_version = HailoFirmwareVersion.construct_from_params(fw_version_major, fw_version_minor, fw_version_revision, is_release,
            extended_context_switch_buffer, HailoFirmwareType.APP)
        self.logger_version = logger_version
        self.board_name = board_name
        self.is_release = is_release
        self.device_architecture = DeviceArchitectureTypes(device_architecture)
        self.serial_number = serial_number
        self.part_number = part_number
        self.product_name = product_name
    
    def _string_field_str(self, string_field):
        # Return <N/A> if the string field is empty
        return string_field.rstrip('\x00') or BOARD_INFO_NOT_CONFIGURED_ATTR

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
        if ((device_arch == DeviceArchitectureTypes.HAILO8) or
            (device_arch == DeviceArchitectureTypes.HAILO8L)):
            return 'hailo8'
        elif ((device_arch == DeviceArchitectureTypes.HAILO15H) or
              (device_arch == DeviceArchitectureTypes.HAILO15M)):
            return 'hailo15'
        elif (device_arch == DeviceArchitectureTypes.HAILO10H):
            return 'hailo10'
        else:
            raise HailoRTException("Unsupported device architecture.")

class CoreInformation(object):
    def __init__(self, fw_version_major, fw_version_minor, fw_version_revision, is_release, extended_context_switch_buffer):
        self.firmware_version = HailoFirmwareVersion.construct_from_params(fw_version_major, fw_version_minor, fw_version_revision, is_release,
            extended_context_switch_buffer, HailoFirmwareType.CORE)
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
    def __init__(self, overcurrent_protection_active, current_overcurrent_zone, red_overcurrent_threshold, overcurrent_throttling_active,
                       temperature_throttling_active, current_temperature_zone, current_temperature_throttling_level,
                       temperature_throttling_levels, orange_temperature_threshold, orange_hysteresis_temperature_threshold, 
                       red_temperature_threshold, red_hysteresis_temperature_threshold, requested_overcurrent_clock_freq, requested_temperature_clock_freq):
        self.overcurrent_protection_active = overcurrent_protection_active
        self.current_overcurrent_zone = current_overcurrent_zone
        self.red_overcurrent_threshold = red_overcurrent_threshold
        self.overcurrent_throttling_active = overcurrent_throttling_active
        self.temperature_throttling_active = temperature_throttling_active
        self.current_temperature_zone = current_temperature_zone
        self.current_temperature_throttling_level = current_temperature_throttling_level
        self.orange_temperature_threshold = orange_temperature_threshold
        self.orange_hysteresis_temperature_threshold = orange_hysteresis_temperature_threshold
        self.red_temperature_threshold = red_temperature_threshold
        self.red_hysteresis_temperature_threshold = red_hysteresis_temperature_threshold
        self.requested_overcurrent_clock_freq = requested_overcurrent_clock_freq
        self.requested_temperature_clock_freq = requested_temperature_clock_freq
        
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
               'Overcurrent Protection Throttling State: {}\n' \
               'Temperature Protection Red Threshold: {}\n' \
               'Temperature Protection Red Hysteresis Threshold: {}\n' \
               'Temperature Protection Orange Threshold: {}\n' \
               'Temperature Protection Orange Hysteresis Threshold: {}\n' \
               'Temperature Protection Throttling State: {}\n' \
               'Temperature Protection Current Zone: {}\n' \
               'Temperature Protection Current Throttling Level: {}\n' \
               'Temperature Protection Throttling Levels: {}' \
               .format(self.overcurrent_protection_active, self.current_overcurrent_zone, self.red_overcurrent_threshold, 
                       self.overcurrent_throttling_active, self.red_temperature_threshold, 
                       self.red_hysteresis_temperature_threshold, self.orange_temperature_threshold, 
                       self.orange_hysteresis_temperature_threshold, self.temperature_throttling_active,
                       self.current_temperature_zone, self.current_temperature_throttling_level, temperature_throttling_levels_str)

class ExtendedDeviceInformation(object):
    def __init__(self, neural_network_core_clock_rate, supported_features, boot_source, lcs, soc_id, eth_mac_address, unit_level_tracking_id, soc_pm_values, gpio_mask):
        self.neural_network_core_clock_rate = neural_network_core_clock_rate
        self.supported_features = SupportedFeatures(supported_features)
        self.boot_source = boot_source
        self.lcs = lcs
        self.soc_id = soc_id
        self.eth_mac_address = eth_mac_address
        self.unit_level_tracking_id = unit_level_tracking_id
        self.soc_pm_values = soc_pm_values
        self.gpio_mask = gpio_mask

    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        INVALID_LCS = 0
        string = 'Neural Network Core Clock Rate: {}MHz\n' \
                 '{}' \
                 'Boot source: {}\n' \
                 'LCS: {}'.format(
            self.neural_network_core_clock_rate / MEGA_MULTIPLIER,
            str(self.supported_features),
            str(self.boot_source.name),
            BOARD_INFO_NOT_CONFIGURED_ATTR if self.lcs == INVALID_LCS else str(self.lcs))
        if any(self.soc_id):
            string += '\nSoC ID: ' + (self.soc_id.hex())

        if any(self.eth_mac_address):
            string += '\nMAC Address: ' + (":".join("{:02X}".format(i) for i in self.eth_mac_address))
        
        if any(self.unit_level_tracking_id):
            string += '\nULT ID: ' + (self.unit_level_tracking_id.hex())
        
        if any(self.soc_pm_values):
            string += '\nPM Values: ' + (self.soc_pm_values.hex())

        if 0 != self.gpio_mask:
            string += '\nGPIO Mask: ' + f'{self.gpio_mask:04x}'

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


class HailoFirmwareVersion(object):
    """Represents a Hailo chip firmware version."""
    CORE_BIT                           = 0x08000000
    EXTENDED_CONTEXT_SWITCH_BUFFER_BIT = 0x40000000
    DEV_BIT                            = 0x80000000
    FW_VERSION_FORMAT = '<III'

    def __init__(self, firmware_version_buffer, is_release, extended_context_switch_buffer, fw_type):
        """Initialize a new Hailo Firmware Version object.

        Args:
            firmware_version_buffer (str): A buffer containing the firmware version struct.
            is_release (bool, optional): Flag indicating if firmware is at develop/release mode.
                                         None indicates unknown
            extended_context_switch_buffer (bool): Flag indicating if firmware has an extended context switch buffer.
            fw_type (HailoFirmwareType): Firmware type
        """
        self.major, self.minor, self.revision = struct.unpack(
            self.FW_VERSION_FORMAT,
            firmware_version_buffer)
        
        self.fw_type = fw_type
        self.mode = HailoFirmwareMode.RELEASE if is_release else HailoFirmwareMode.DEVELOP
        self.extended_context_switch_buffer = extended_context_switch_buffer
        
        self.revision &= ~(self.CORE_BIT | self.EXTENDED_CONTEXT_SWITCH_BUFFER_BIT | self.DEV_BIT)

    def __str__(self):
        """Returns:
            str: Firmware version in a human readable format.
        """
        return '{}.{}.{} ({},{}{})'.format(self.major, self.minor, self.revision, self.mode.value, self.fw_type.value,
            ',extended context switch buffer' if self.extended_context_switch_buffer else '')

    @classmethod
    def construct_from_params(cls, major, minor, revision, is_release, extended_context_switch_buffer, fw_type):
        """Returns:
            class HailoFirmwareVersion : with the given Firmware version.
        """
        return cls(struct.pack(HailoFirmwareVersion.FW_VERSION_FORMAT, major, minor, revision), is_release,
            extended_context_switch_buffer, fw_type)

    @property
    def comparable_value(self):
        """A value that could be compared to other firmware versions."""
        return (self.major << 64) + (self.minor << 32) + (self.revision)

    def __hash__(self):
        return self.comparable_value

    def __eq__(self, other):
        return self.comparable_value == other.comparable_value

    def __lt__(self, other):
        return self.comparable_value < other.comparable_value

    def check_protocol_compatibility(self, other):
        return ((self.major == other.major) and (self.minor == other.minor))

class SupportedFeatures(object):
    def __init__(self, supported_features):
        self.ethernet = supported_features.ethernet
        self.pcie = supported_features.pcie
        self.current_monitoring = supported_features.current_monitoring
        self.mdio = supported_features.mdio
        self.power_measurement = supported_features.power_measurement
    
    def _feature_str(self, feature_name, is_feature_enabled):
        return '{}: {}\n'.format(feature_name, 'Enabled' if is_feature_enabled else 'Disabled')

    def __str__(self):
        """Returns:
            str: Human readable string.
        """
        return 'Device supported features: \n' + \
            self._feature_str('Ethernet', self.ethernet) + \
            self._feature_str('PCIE', self.pcie) + \
            self._feature_str('Current Monitoring', self.current_monitoring) + \
            self._feature_str('MDIO', self.mdio) + \
            self._feature_str('Power Measurement', self.power_measurement)

    def __repr__(self):
        """Returns:
            str: Human readable string.
        """
        return self.__str__()

    def _is_feature_enabled(self, feature):
        return (self.supported_features & feature) != 0

class Control:
    """The control object of this device, which implements the control API of the Hailo device.
    Should be used only from Device.control"""

    WORD_SIZE = 4

    def __init__(self, device: '_pyhailort.Device'):
        self.__device = device

        # TODO: should remove?
        if sys.platform != "win32":
            signal.pthread_sigmask(signal.SIG_BLOCK, [signal.SIGWINCH])

        self._identify_info = self.identify()

    @property
    def _device(self):
        if not self.__device.is_valid():
            raise HailoRTInvalidOperationException("The device in use has been released. "
                "This can happen if 'device.release()' has been called, or one-liner usage of control 'Device().control.XX()'")
        return self.__device

    @property
    def device_id(self):
        """Getter for the device_id.

        Returns:
            str: A string ID of the device. BDF for PCIe devices, IP address for Ethernet devices, "Integrated" for integrated nnc devices.
        """
        return self._device.device_id

    def open(self):
        """Initializes the resources needed for using a control device."""
        pass

    def close(self):
        """Releases the resources that were allocated for the control device."""
        pass

    def chip_reset(self):
        """Resets the device (chip reset)."""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.CHIP)

    def nn_core_reset(self):
        """Resets the nn_core."""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.NN_CORE)

    def soft_reset(self):
        """reloads the device firmware (soft reset)"""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.SOFT)

    def forced_soft_reset(self):
        """reloads the device firmware (forced soft reset)"""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.FORCED_SOFT)

    def read_memory(self, address, data_length):
        """Reads memory from the Hailo chip. Byte order isn't changed. The core uses little-endian
        byte order.

        Args:
            address (int): Physical address to read from.
            data_length (int): Size to read in bytes.

        Returns:
            list of str: Memory read from the chip, each index in the list is a byte
        """
        with ExceptionWrapper():
            return self._device.read_memory(address, data_length)

    def write_memory(self, address, data_buffer):
        """Write memory to Hailo chip. Byte order isn't changed. The core uses little-endian byte
        order.

        Args:
            address (int): Physical address to write to.
            data_buffer (list of str): Data to write.
        """
        with ExceptionWrapper():
            return self._device.write_memory(address, data_buffer, len(data_buffer))

    def power_measurement(self, dvm=DvmTypes.AUTO, measurement_type=PowerMeasurementTypes.AUTO):
        """Perform a single power measurement on an Hailo chip. It works with the default settings
        where the sensor returns a new value every 2.2 ms without averaging the values.

        Args:
            dvm (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes`):
                Which DVM will be measured. Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) will be different according to the board: \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for EVB is an approximation to the total power consumption of the chip in PCIe setups.
                 It sums :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.VDD_CORE`,
                 :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.MIPI_AVDD` and :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AVDD_H`.
                 Only :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER` can measured with this option. \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for platforms supporting current monitoring (such as M.2 and mPCIe): :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.OVERCURRENT_PROTECTION`
            measurement_type
             (:class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`):
             The type of the measurement.

        Returns:
            float: The measured power. \n
            For :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`: \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.SHUNT_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.BUS_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER`: Unit is W. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.CURRENT`: Unit is mA. \n


        Note:
            This function can perform measurements for more than just power. For all supported
            measurement types, please look at
            :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`.
        """

        with ExceptionWrapper():
            return self._device.power_measurement(dvm, measurement_type)

    def start_power_measurement(self, averaging_factor=AveragingFactor.AVERAGE_256, sampling_period=SamplingPeriod.PERIOD_1100us):
        """Start performing a long power measurement.

        Args:
            averaging_factor (:class:`~hailo_platform.pyhailort.pyhailort.AveragingFactor`):
                Number of samples per time period, sensor configuration value.
            sampling_period (:class:`~hailo_platform.pyhailort.pyhailort.SamplingPeriod`):
                Related conversion time, sensor configuration value. The sensor samples the power
                every ``sampling_period`` [ms] and averages every ``averaging_factor`` samples. The
                sensor provides a new value every: 2 * sampling_period * averaging_factor [ms]. The
                firmware wakes up every ``delay`` [ms] and checks the sensor. If there is a new`
                value to read from the sensor, the firmware reads it.  Note that the average
                calculated by the firmware is "average of averages", because it averages values
                that have already been averaged by the sensor.
        """
        with ExceptionWrapper():
            return self._device.start_power_measurement(averaging_factor, sampling_period)

    def stop_power_measurement(self):
        """Stop performing a long power measurement.
        Calling the function eliminates the start function settings for the averaging the samples,
        and returns to the default values, so the sensor will return a new value every 2.2 ms
        without averaging values.
        """
        with ExceptionWrapper():
            return self._device.stop_power_measurement()

    def set_power_measurement(self, buffer_index=MeasurementBufferIndex.MEASUREMENT_BUFFER_INDEX_0, dvm=DvmTypes.AUTO, measurement_type=PowerMeasurementTypes.AUTO):
        """Set parameters for long power measurement on an Hailo chip.

        Args:
            buffer_index (:class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex`): Index of the buffer on the firmware the data would be saved at.
                Default is :class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex.MEASUREMENT_BUFFER_INDEX_0`
            dvm (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes`):
                Which DVM will be measured. Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) will be different according to the board: \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for EVB is an approximation to the total power consumption of the chip in PCIe setups.
                 It sums :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.VDD_CORE`,
                 :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.MIPI_AVDD` and :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AVDD_H`.
                 Only :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER` can measured with this option. \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for platforms supporting current monitoring (such as M.2 and mPCIe): :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.OVERCURRENT_PROTECTION`
            measurement_type
             (:class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`):
             The type of the measurement.

        Note:
            This function can perform measurements for more than just power. For all supported measurement types
            view :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`
        """
        with ExceptionWrapper():
            return self._device.set_power_measurement(buffer_index, dvm, measurement_type)

    def get_power_measurement(self, buffer_index=MeasurementBufferIndex.MEASUREMENT_BUFFER_INDEX_0, should_clear=True):
        """Read measured power from a long power measurement

        Args:
            buffer_index (:class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex`): deprecated.
            should_clear (bool): Flag indicating if the results saved at the firmware will be deleted after reading.

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementData`:
             Object containing measurement data \n
            For :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`: \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.SHUNT_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.BUS_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER`: Unit is W. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.CURRENT`: Unit is mA. \n

        Note:
            This function can perform measurements for more than just power.
            For all supported measurement types view
            :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`.
        """

        with ExceptionWrapper():
            return self._device.get_power_measurement(buffer_index, should_clear)

    def _examine_user_config(self):
        with ExceptionWrapper():
            return self._device.examine_user_config()

    def read_user_config(self):
        """Read the user configuration section as binary data.

        Returns:
            str: User config as a binary buffer.
        """
        with ExceptionWrapper():
            return self._device.read_user_config()

    def write_user_config(self, configuration):
        """Write the user configuration.

        Args:
            configuration (str): A binary representation of a Hailo device configuration.
        """
        with ExceptionWrapper():
            return self._device.write_user_config(configuration)

    def _erase_user_config(self):
        with ExceptionWrapper():
            return self._device.erase_user_config()

    def read_board_config(self):
        """Read the board configuration section as binary data.

        Returns:
            str: Board config as a binary buffer.
        """
        with ExceptionWrapper():
            return self._device.read_board_config()

    def write_board_config(self, configuration):
        """Write the static configuration.

        Args:
            configuration (str): A binary representation of a Hailo device configuration.
        """
        with ExceptionWrapper():
            return self._device.write_board_config(configuration)

    def identify(self):
        """Gets the Hailo chip identification.

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.BoardInformation`
        """
        with ExceptionWrapper():
            response = self._device.identify()
        board_information = BoardInformation(response.protocol_version, response.fw_version.major,
            response.fw_version.minor, response.fw_version.revision, response.logger_version,
            response.board_name, response.is_release, response.extended_context_switch_buffer,
            int(response.device_architecture), response.serial_number, response.part_number,
            response.product_name)
        return board_information

    def core_identify(self):
        """Gets the Core Hailo chip identification.

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.CoreInformation`
        """
        with ExceptionWrapper():
            response =  self._device.core_identify()
        core_information = CoreInformation(response.fw_version.major, response.fw_version.minor, 
            response.fw_version.revision, response.is_release, response.extended_context_switch_buffer)
        return core_information

    def set_fw_logger(self, level, interface_mask):
        """Configure logger level and interface of sending.

        Args:
            level (FwLoggerLevel):    The minimum logger level.
            interface_mask (int):     Output interfaces (mix of FwLoggerInterface).
        """
        with ExceptionWrapper():
            return self._device.set_fw_logger(level, interface_mask)

    def set_throttling_state(self, should_activate):
        """Change throttling state of temperature protection and overcurrent protection components.
           In case that change throttling state of temperature protection didn't succeed,
           the change throttling state of overcurrent protection is executed.

        Args:
            should_activate (bool):   Should be true to enable or false to disable. 
        """
        with ExceptionWrapper():
            return self._device.set_throttling_state(should_activate)

    def get_throttling_state(self):
        """Get the current throttling state of temperature protection and overcurrent protection components.
           If any throttling is enabled, the function return true.

        Returns:
            bool: true if temperature or overcurrent protection throttling is enabled, false otherwise.
        """
        with ExceptionWrapper():
            return self._device.get_throttling_state()

    def _set_overcurrent_state(self, should_activate):
        """Control whether the overcurrent protection is enabled or disabled.

        Args:
            should_activate (bool):   Should be true to enable or false to disable. 
        """
        with ExceptionWrapper():
            return self._device._set_overcurrent_state(should_activate)

    def _get_overcurrent_state(self):
        """Get the overcurrent protection state.
        
        Returns:
            bool: true if overcurrent protection is enabled, false otherwise.
        """
        with ExceptionWrapper():
            return self._device._get_overcurrent_state()

    def read_register(self, address):
        """Read the value of a register from a given address.

        Args:
            address (int): Address to read register from.

        Returns:
            int: Value of the register
        """
        register_value, = struct.unpack('!I', self.read_memory(address, type(self).WORD_SIZE))
        return register_value

    def set_bit(self, address, bit_index):
        """Set (turn on) a specific bit at a register from a given address.

        Args:
            address (int) : Address of the register to modify.
            bit_index (int) : Index of the bit that would be set.
        """
        register_value = self.read_register(address)
        register_value |= 1 << bit_index
        self.write_memory(address, struct.pack('!I', register_value))

    def reset_bit(self, address, bit_index):
        """Reset (turn off) a specific bit at a register from a given address.

        Args:
            address (int) :  Address of the register to modify.
            bit_index (int) : Index of the bit that would be reset.
        """
        register_value = self.read_register(address)
        register_value &= ~(1 << bit_index)
        self.write_memory(address, struct.pack('!I', register_value))

    def firmware_update(self, firmware_binary, should_reset=True):
        """Update firmware binary on the flash. 
        
        Args:
            firmware_binary (bytes): firmware binary stream.
            should_reset (bool): Should a reset be performed after the update (to load the new firmware)
        """
        with ExceptionWrapper():
            return self._device.firmware_update(firmware_binary, len(firmware_binary), should_reset)

    def second_stage_update(self, second_stage_binary):
        """Update second stage binary on the flash
        
        Args:
            second_stage_binary (bytes): second stage binary stream.
        """
        with ExceptionWrapper():
            return self._device.second_stage_update(second_stage_binary, len(second_stage_binary))

    def store_sensor_config(self, section_index, reset_data_size, sensor_type, config_file_path,
                            config_height=0, config_width=0, config_fps=0, config_name=None):
            
        """Store sensor configuration to Hailo chip flash memory.
        
        Args:
            section_index (int): Flash section index to write to. [0-6]
            reset_data_size (int): Size of reset configuration.
            sensor_type (:class:`~hailo_platform.pyhailort.pyhailort.SensorConfigTypes`): Sensor type.
            config_file_path (str): Sensor configuration file path.
            config_height (int): Configuration resolution height.
            config_width (int): Configuration resolution width.
            config_fps (int): Configuration FPS.
            config_name (str): Sensor configuration name.
        """
        if config_name is None:
            config_name = "UNINITIALIZED"

        with ExceptionWrapper():
            return self._device.sensor_store_config(section_index, reset_data_size, sensor_type, config_file_path,
            config_height, config_width, config_fps, config_name)

    def store_isp_config(self, reset_config_size, isp_static_config_file_path, isp_runtime_config_file_path,
                         config_height=0, config_width=0, config_fps=0, config_name=None):
        """Store sensor isp configuration to Hailo chip flash memory.

        Args:
            reset_config_size (int): Size of reset configuration.
            isp_static_config_file_path (str): Sensor isp static configuration file path.
            isp_runtime_config_file_path (str): Sensor isp runtime configuration file path.
            config_height (int): Configuration resolution height.
            config_width (int): Configuration resolution width.
            config_fps (int): Configuration FPS.
            config_name (str): Sensor configuration name.
        """
        if config_name is None:
            config_name = "UNINITIALIZED"

        with ExceptionWrapper():
            return self._device.store_isp_config(reset_config_size, config_height, config_width, 
            config_fps, isp_static_config_file_path, isp_runtime_config_file_path, config_name)

    def get_sensor_sections_info(self):
        """Get sensor sections info from Hailo chip flash memory.

        Returns:
            Sensor sections info read from the chip flash memory.
        """
        with ExceptionWrapper():
            return self._device.sensor_get_sections_info()

    def sensor_set_generic_i2c_slave(self, slave_address, register_address_size, bus_index, should_hold_bus, endianness):
        """Set a generic I2C slave for sensor usage.

        Args:
            sequence (int): Request/response sequence.
            slave_address (int): The address of the I2C slave.
            register_address_size (int): The size of the offset (in bytes).
            bus_index (int): The number of the bus the I2C slave is behind.
            should_hold_bus (bool): Hold the bus during the read.
            endianness (:class:`~hailo_platform.pyhailort.pyhailort.Endianness`):
                Big or little endian.
        """
        with ExceptionWrapper():
            return self._device.sensor_set_generic_i2c_slave(slave_address, register_address_size, bus_index, should_hold_bus, endianness)

    def set_sensor_i2c_bus_index(self, sensor_type, i2c_bus_index):
        """Set the I2C bus to which the sensor of the specified type is connected.
  
        Args:
            sensor_type (:class:`~hailo_platform.pyhailort.pyhailort.SensorConfigTypes`): The sensor type.
            i2c_bus_index (int): The I2C bus index of the sensor.
        """
        with ExceptionWrapper():
            return self._device.sensor_set_i2c_bus_index(sensor_type, i2c_bus_index)

    def load_and_start_sensor(self, section_index):
        """Load the configuration with I2C in the section index.
  
        Args:
            section_index (int): Flash section index to load config from. [0-6]
        """
        with ExceptionWrapper():
            return self._device.sensor_load_and_start_config(section_index)

    def reset_sensor(self, section_index):
        """Reset the sensor that is related to the section index config.

        Args:
            section_index (int): Flash section index to reset. [0-6]
        """
        with ExceptionWrapper():
            return self._device.sensor_reset(section_index)

    def wd_enable(self, cpu_id):
        """Enable firmware watchdog.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
        """
        with ExceptionWrapper():
            return self._device.wd_enable(cpu_id)

    def wd_disable(self, cpu_id):
        """Disable firmware watchdog.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
        """
        with ExceptionWrapper():
            return self._device.wd_disable(cpu_id)

    def wd_config(self, cpu_id, wd_cycles, wd_mode):
        """Configure a firmware watchdog.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
            wd_cycles (int): number of cycles until watchdog is triggered.
            wd_mode (int): 0 - HW/SW mode, 1 -  HW only mode
        """
        with ExceptionWrapper():
            return self._device.wd_config(cpu_id, wd_cycles, wd_mode)

    def previous_system_state(self, cpu_id):
        """Read the FW previous system state.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
        """
        with ExceptionWrapper():
            return self._device.previous_system_state(cpu_id)

    def get_chip_temperature(self):
        """Returns the latest temperature measurements from the two internal temperature sensors of the Hailo chip.

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.TemperatureInfo`:
             Temperature in celsius of the two internal temperature sensors (TS), and a sample
             count (a running 16-bit counter)
        """
        with ExceptionWrapper():
            return self._device.get_chip_temperature()

    def get_extended_device_information(self):
        """Returns extended information about the device

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.ExtendedDeviceInformation`:

        """
        with ExceptionWrapper():
            response = self._device.get_extended_device_information()
        device_information = ExtendedDeviceInformation(response.neural_network_core_clock_rate,
            response.supported_features, response.boot_source, response.lcs, response.soc_id,
            response.eth_mac_address, response.unit_level_tracking_id, response.soc_pm_values, response.gpio_mask)
        return device_information

    def _get_health_information(self):
        with ExceptionWrapper():
            response = self._device._get_health_information()

        health_information = HealthInformation(response.overcurrent_protection_active, response.current_overcurrent_zone, response.red_overcurrent_threshold,
                    response.overcurrent_throttling_active, response.temperature_throttling_active, response.current_temperature_zone, response.current_temperature_throttling_level, 
                    response.temperature_throttling_levels, response.orange_temperature_threshold, response.orange_hysteresis_temperature_threshold,
                    response.red_temperature_threshold, response.red_hysteresis_temperature_threshold, response.requested_overcurrent_clock_freq,
                    response.requested_temperature_clock_freq)
        return health_information

    def set_pause_frames(self, rx_pause_frames_enable):
        """Enable/Disable Pause frames.

        Args:
            rx_pause_frames_enable (bool): False for disable, True for enable.
        """
        with ExceptionWrapper():
            return self._device.set_pause_frames(rx_pause_frames_enable)

    def test_chip_memories(self):
        """test all chip memories using smart BIST

        """
        with ExceptionWrapper():
            return self._device.test_chip_memories()

    def set_notification_callback(self, callback_func, notification_id, opaque):
        """Set a callback function to be called when a notification is received.

        Args:
            callback_func (function): Callback function with the parameters (device, notification, opaque).
                Note that throwing exceptions is not supported and will cause the program to terminate with an error!
            notification_id (NotificationId): Notification ID to register the callback to.
            opauqe (object): User defined data.

        Note:
            The notifications thread is started and closed in the use_device() context, so
            notifications can only be received there.
        """
        with ExceptionWrapper():
            return self._device.set_notification_callback(callback_func, notification_id, opaque)

    def remove_notification_callback(self, notification_id):
        """Remove a notification callback which was already set.

        Args:
            notification_id (NotificationId): Notification ID to remove the callback from.
        """
        with ExceptionWrapper():
            return self._device.remove_notification_callback(notification_id)

    def _get_device_handle(self):
        return self._device


class Device:
    """ Hailo device object representation (for inference use VDevice)"""

    @classmethod
    def scan(cls):
        """Scans for all devices on the system.

        Returns:
            list of str, device ids.
        """
        return _pyhailort.Device.scan()

    def __init__(self, device_id=None):
        """Create the Hailo device object.

        Args:
            device_id (str): Device id string, can represent several device types:
                                [-] for pcie devices - pcie bdf (XXXX:XX:XX.X)
                                [-] for ethernet devices - ip address (xxx.xxx.xxx.xxx)
        """
        gc.collect()

        # Device __del__ function tries to release self._device.
        # to avoid AttributeError if the __init__ func fails, we set it to None first.
        # https://stackoverflow.com/questions/6409644/is-del-called-on-an-object-that-doesnt-complete-init
        self._device = None

        if device_id is None:
            system_device_ids = Device.scan()
            if len(system_device_ids) == 0:
                raise HailoRTException('HailoRT device not found in the system')
            device_id = system_device_ids[0]

        self._device_id = device_id
        self._device = _pyhailort.Device.create(device_id)
        self._loaded_network_groups = []
        self._creation_pid = os.getpid()
        self._control_object = Control(self._device)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()
        return False

    def __del__(self):
        self.release()

    def __repr__(self) -> str:
        return f'Device({self._device_id!r})'

    def release(self):
        """Release the allocated resources of the device. This function should be called when working with the device not as context-manager."""

        if self._device is not None:
            with ExceptionWrapper():
                self._device.release()
            self._device = None

    @property
    def device_id(self):
        """Getter for the device_id.

        Returns:
            str: A string ID of the device. BDF for PCIe devices, IP address for Ethernet devices, "Integrated" for integrated nnc devices.
        """
        return self._device_id

    @property
    def control(self):
        """
        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.Control`: the control object of this device, which
            implements the control API of the Hailo device.

        .. attention:: Use the low level control API with care.
        """
        return self._control_object

    @property
    def loaded_network_groups(self):
        """Getter for the property _loaded_network_groups.

        Returns:
            list of :obj:`ConfiguredNetwork`: List of the the configured network groups loaded on the device.
        """
        return self._loaded_network_groups

    @property
    def _loaded_network_group(self):
        if len(self._loaded_network_groups) != 1:
            raise HailoRTException("Access to network group is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0]


class AsyncInferCompletionInfo:
    """
    Holds information about the async infer job
    """

    def  __init__(self, exception):
        """
        Args:
            exception (:obj:`HailoRTException`): an exception corresponding to the error that happened inside the async infer job.
        """
        self._exception = exception

    @property
    def exception(self):
        """
        Returns the exception that was set on this Infer job. if the job finished succesfully, returns None.
        """
        return self._exception


class InferModel:
    """
    Contains all of the necessary information for configuring the network for inference.
    This class is used to set up the model for inference and includes methods for setting and getting the model's parameters.
    By calling the configure function, the user can create a :obj:`ConfiguredInferModel` object, which is used to run inference.
    """
    class InferStream:
        """
        Represents the parameters of a stream.
        In default, the stream's parameters are set to the default values of the model.
        The user can change the stream's parameters by calling the setter functions.
        """
        def __init__(self, infer_stream):
            #"""
            #Args:
            #    infer_stream (_pyhailort.InferStream): The C++ InferStream object.
            #"""
            self._infer_stream = infer_stream

        @property
        def name(self):
            """
            Returns:
                name (str): the name of the edge.
            """
            with ExceptionWrapper():
                return self._infer_stream.name()

        @property
        def shape(self):
            """
            Returns:
                shape (list[int]): the shape of the edge.
            """
            with ExceptionWrapper():
                return self._infer_stream.shape()

        @property
        def format(self):
            """
            Returns:
                format (_pyhailort.hailo_format_t): the format of the edge.
            """
            with ExceptionWrapper():
                return self._infer_stream.format()

        def set_format_type(self, type):
            """
            Set the format type of the stream.

            Args:
                type (_pyhailort.hailo_format_type_t): the format type
            """
            with ExceptionWrapper():
                self._infer_stream.set_format_type(type)

        def set_format_order(self, order):
            """
            Set the format order of the stream.

            Args:
                order (_pyhailort.hailo_format_order_t): the format order
            """
            with ExceptionWrapper():
                self._infer_stream.set_format_order(order)

        @property
        def quant_infos(self):
            """
            Returns:
                quant_infos (list[_pyhailort.hailo_quant_info_t]): List of the quantization information of the edge.
            """
            with ExceptionWrapper():
                return self._infer_stream.get_quant_infos()

        @property
        def is_nms(self):
            """
            Returns:
                is_nms (bool): whether the stream is NMS.
            """
            with ExceptionWrapper():
                return self._infer_stream.is_nms()

        def set_nms_score_threshold(self, threshold):
            """
            Set NMS score threshold, used for filtering out candidates. Any box with score<TH is suppressed.

            Args:
                threshold (float): NMS score threshold to set.

            Note:
                This function is invalid in cases where the edge has no NMS operations on the CPU. It will not fail,
                but make the :func:`~hailo_platform.pyhailort.pyhailort.pyhailort.InferModel.configure()` function fail.
            """
            with ExceptionWrapper():
                self._infer_stream.set_nms_score_threshold(threshold)

        def set_nms_iou_threshold(self, threshold):
            """
            Set NMS intersection over union overlap Threshold,
            used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.

            Args:
                threshold (float): NMS IoU threshold to set.

            Note:
                This function is invalid in cases where the edge has no NMS operations on the CPU. It will not fail,
                but make the `configure()` function fail.
            """
            with ExceptionWrapper():
                self._infer_stream.set_nms_iou_threshold(threshold)

        def set_nms_max_proposals_per_class(self, max_proposals):
            """
            Set a limit for the maximum number of boxes per class.

            Args:
                max_proposals (int): NMS max proposals per class to set.

            Note:
                This function is invalid in cases where the edge has no NMS operations on the CPU. It will not fail,
                but make the `configure()` function fail.
            """
            with ExceptionWrapper():
                self._infer_stream.set_nms_max_proposals_per_class(max_proposals)

        def set_nms_max_accumulated_mask_size(self, max_accumulated_mask_size):
            """
            Set maximum accumulated mask size for all the detections in a frame.

            Args:
                max_accumalated_mask_size (int): NMS max accumulated mask size.

            Note:
                Used in order to change the output buffer frame size in cases where the
                output buffer is too small for all the segmentation detections.

            Note:
                This function is invalid in cases where the edge has no NMS operations on the CPU. It will not fail,
                but make the `configure()` function fail.
            """
            with ExceptionWrapper():
                self._infer_stream.set_nms_max_accumulated_mask_size(max_accumulated_mask_size)


    def __init__(self, infer_model, hef_path):
        #"""
        #Args:
        #    infer_model (_pyhailort.InferModel): The internal InferModel object.
        #    hef_path (str): The path to the HEF file.
        #"""
        self._infer_model = infer_model
        self._hef_path = hef_path
        self._hef = None

    @property
    def hef(self):
        """
        Returns:
            :class:`HEF`: the HEF object of the model
        """
        # TODO: https://hailotech.atlassian.net/browse/HRT-13659
        if not self._hef:
            with ExceptionWrapper():
                self._hef = HEF(self._hef_path)

        return self._hef

    def set_batch_size(self, batch_size):
        """
        Sets the batch size of the InferModel. This parameter determines the number of frames to be sent for inference
        in a single batch. If a scheduler is enabled, this parameter determines the 'burst size': the max number of
        frames after which the scheduler will attempt to switch to another model.

        Note:
            The default value is `HAILO_DEFAULT_BATCH_SIZE` - which means the batch is determined by HailoRT automatically.

        Args:
            batch_size (int): The new batch size to be set.
        """
        with ExceptionWrapper():
            self._infer_model.set_batch_size(batch_size)

    def set_power_mode(self, power_mode):
        """
        Sets the power mode of the InferModel

        Args:
            power_mode (_pyhailort.hailo_power_mode_t): The power mode to set.
        """
        with ExceptionWrapper():
            self._infer_model.set_power_mode(power_mode)

    def _set_enable_kv_cache(self, enable_kv_cache):
        with ExceptionWrapper():
            self._infer_model.set_enable_kv_cache(enable_kv_cache)

    def configure(self):
        """
        Configures the InferModel object. Also checks the validity of the configuration's formats.

        Returns:
            configured_infer_model (:class:`ConfiguredInferModel`): The configured :class:`InferModel` object.

        Raises:
            :class:`HailoRTException`: In case the configuration is invalid (example: see :func:`InferStream.set_nms_iou_threshold`).

        Note:
            A :obj:`ConfiguredInferModel` should be used inside a context manager, and should not be passed to a different process.
        """
        with ExceptionWrapper():
            configured_infer_model_cpp_obj = self._infer_model.configure()
            return ConfiguredInferModel(configured_infer_model_cpp_obj, self)

    @property
    def input_names(self):
        """
        Returns:
            names (list[str]): The input names of the :class:`InferModel`.
        """
        with ExceptionWrapper():
            return self._infer_model.get_input_names()

    @property
    def output_names(self):
        """
        Returns:
            names (list[str]): The output names of the :class:`InferModel`.
        """
        with ExceptionWrapper():
            return self._infer_model.get_output_names()

    @property
    def inputs(self):
        """
        Returns:
            inputs (list[`InferStream`]): List of input :class:`InferModel.InferStream`.
        """
        with ExceptionWrapper():
            return [self.InferStream(infer_stream) for infer_stream in self._infer_model.inputs()]

    @property
    def outputs(self):
        """
        Returns:
            outputs (list[`InferStream`]): List of output :class:`InferModel.InferStream`.
        """
        with ExceptionWrapper():
            return [self.InferStream(infer_stream) for infer_stream in self._infer_model.outputs()]

    def input(self, name=""):
        """
        Gets an input's :class:`InferModel.InferStream`.

        Args:
            name (str, optional): the name of the input stream. Required in case of multiple inputs.

        Returns:
            :class:`ConfiguredInferModel.Bindings.InferStream` - the input infer stream of the configured infer model.

        Raises:
            :class:`HailoRTNotFoundException` in case a non-existing input is requested or no name is given
            but multiple inputs exist.
        """
        with ExceptionWrapper():
            return self.InferStream(self._infer_model.input(name))

    def output(self, name=""):
        """
        Gets an output's :class:`InferModel.InferStream`.

        Args:
            name (str, optional): the name of the output stream. Required in case of multiple outputs.

        Returns:
            :obj:`ConfiguredInferModel.Bindings.InferStream` - the output infer stream of the configured infer model.

        Raises:
            :class:`HailoRTNotFoundException` in case a non-existing output is requested or no name is given
            but multiple outputs exist.
        """
        with ExceptionWrapper():
            return self.InferStream(self._infer_model.output(name))


class ConfiguredInferModel:
    """
    Configured :class:`InferModel` that can be used to perform an asynchronous inference.

    Note:
        Passing an instance of :class:`ConfiguredInferModel` to a different process is not supported and would lead to an undefined behavior.
    """

    @dataclass
    class NmsTransformationInfo:
        """
        class for NMS transformation info.
        """
        format_order: FormatOrder
        input_height: int
        input_width: int
        number_of_classes: int
        max_bboxes_per_class: int
        quant_info: _pyhailort.QuantInfo
        output_dtype: numpy.dtype = numpy.dtype('float32')

    @dataclass
    class NmsHailoTransformationInfo(NmsTransformationInfo):
        """
        class for NMS transformation info when using hailo format
        """
        use_tf_nms_format: bool = False

    @dataclass
    class NmsTfTransformationInfo(NmsTransformationInfo):
        """
        class for NMS transformation info when using tf format
        """
        use_tf_nms_format: bool = True

    class Bindings:
        """
        Represents an asynchronous infer request - holds the input and output buffers of the request.
        A request represents a single frame.
        """

        class InferStream:
            """
            Holds the input and output buffers of the Bindings infer request
            """
            def __init__(self, infer_stream, nms_info=None):
                #"""
                #Args:
                #    infer_stream (_pyhailort.InferStream): The internal infer stream object.
                #    nmw_info (class:`ConfiguredInferModel.NmsTransformationInfo`, optional): The NMS transformation info.
                #"""
                self._infer_stream = infer_stream
                self._buffer = None
                if nms_info:
                    self._nms_info = nms_info
                    self._quantized_empty_bbox = numpy.asarray(
                        [0] * BBOX_PARAMS,
                        dtype=nms_info.output_dtype,
                    )

                    HailoRTTransformUtils.dequantize_output_buffer_in_place(
                        self._quantized_empty_bbox,
                        nms_info.output_dtype,
                        BBOX_PARAMS,
                        nms_info.quant_info,
                    )
                else:
                    self._nms_info = None

            @staticmethod
            def _validate_c_contiguous(buffer):
                if not buffer.flags.c_contiguous:
                    raise HailoRTException("Buffer must be C_CONTIGUOUS")

            def set_buffer(self, buffer):
                """
                Sets the edge's buffer to a new one.

                Args:
                    buffer (numpy.array): The new buffer to set. The array's shape should match the edge's shape.
                """
                with ExceptionWrapper():
                    self._validate_c_contiguous(buffer)
                    self._infer_stream.set_buffer(buffer)

                self._buffer = buffer

            def get_buffer(self, tf_format=False):
                """
                Gets the edge's buffer.

                Args:
                    tf_format (bool, optional): Whether the output format is tf or hailo. Relevant for NMS outputs. The output
                        can be re-formatted into two formats (TF, Hailo) and the user through choosing the True/False function
                        parameter, can decide which format to receive. Does not support format order HAILO_NMS_BY_SCORE.

                        For detection outputs:
                        TF format is an :obj:`numpy.array` with shape [number of classes, bounding box params, max bounding boxes per class]
                        where the 2nd dimension (bounding box params) is of a fixed length of 5 (y_min, x_min, y_max, x_max, score).

                        hailo HAILO_NMS_BY_CLASS format is a list of :obj:`numpy.array` where each array represents the detections for a specific class:
                        [cls0_detections, cls1_detections, ...]. The length of the list is the number of classes.
                        Each :obj:`numpy.array` shape is (number of detections, bounding box params) where the 2nd dimension
                        (bounding box params) is of a fixed length of 5 (y_min, x_min, y_max, x_max, score).

                        hailo HAILO_NMS_BY_SCORE format is a list of detections where each detection is an :obj:`Detection` object.
                        The detections are sorted decreasingly by score.

                        For segmentation outputs:
                        TF format is an :obj:`numpy.array` with shape [1, image_size + number_of_params, max bounding boxes per class]
                        where the 2nd dimension (image_size + number_of_params) is calculated as: mask (image_width * image_height) + (y_min, x_min, y_max, x_max, score, class_id).
                        The mask is a binary mask of the segmentation output where the ROI (region of interest) is mapped to 1 and the background is mapped to 0.

                        Hailo format is a list of detections: [detecion0, detection1, ... detection_m]
                        where each detection is an :obj:`DetectionWithByteMask`. The detections are sorted decreasingly by score.

                Returns:
                    buffer (numpy.array): the buffer of the edge.
                """
                buffer = self._buffer

                if tf_format is None:
                    # A user would prefer the plain buffer, with no transformation.
                    # This is especially useful when the output is not ready, which would potentially cause the NMS transformation to fail.
                    return buffer
                if self._nms_info:
                    nms_info_class = ConfiguredInferModel.NmsTfTransformationInfo if tf_format else ConfiguredInferModel.NmsHailoTransformationInfo
                    nms_info = nms_info_class(**self._nms_info.__dict__)

                    if nms_info.format_order == FormatOrder.HAILO_NMS_WITH_BYTE_MASK:
                        if nms_info.use_tf_nms_format:
                            converted_output_buffer = numpy.empty(
                                [
                                    nms_info.max_bboxes_per_class,
                                    (nms_info.input_height * nms_info.input_width + BBOX_WITH_MASK_PARAMS),
                                ],
                                dtype=nms_info.output_dtype,
                            )

                            buffer = HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_tf_format_single_frame(
                                self._buffer,
                                converted_output_buffer,
                                nms_info.number_of_classes,
                                nms_info.max_bboxes_per_class,
                                nms_info.input_height,
                                nms_info.input_width,
                            )
                        else:
                            buffer = HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_hailo_format_single_frame(self._buffer)
                    elif nms_info.format_order in [FormatOrder.HAILO_NMS_BY_CLASS]:
                        if nms_info.use_tf_nms_format:
                            nms_shape = [
                                nms_info.number_of_classes,
                                BBOX_PARAMS,
                                nms_info.max_bboxes_per_class,
                            ]

                            shape = [1, *nms_shape]
                            flat_result = self._buffer.reshape(-1)

                            buffer = HailoRTTransformUtils.output_raw_buffer_to_nms_tf_format(
                                flat_result,
                                shape,
                                nms_info.output_dtype,
                                self._quantized_empty_bbox,
                            )[0]
                        else:
                            buffer = HailoRTTransformUtils.output_raw_buffer_to_nms_format_single_frame(
                                self._buffer,
                                nms_info.number_of_classes,
                            )
                    elif nms_info.format_order in [FormatOrder.HAILO_NMS_BY_SCORE]:
                        if nms_info.use_tf_nms_format:
                            raise HailoRTException(f"Use of tf format is unsupported for format order: {nms_info.format_order}.")
                        else:
                            buffer = HailoRTTransformUtils._output_raw_buffer_to_nms_by_score_format_single_frame(self._buffer)
                    else:
                        raise HailoRTException(f"Unsupported NMS format order: {nms_info.format_order}.")

                return buffer

        def __init__(self, bindings, input_names, output_names, nms_infos):
            #"""
            #Args:
            #    bindings (_pyhailort.ConfiguredInferModelBindingsWrapper): The internal bindings object.
            #    input_names (list[str]): The input names of the model.
            #    output_names (list[str]): The output names of the model.
            #    nms_infos (dict[str : class:`ConfiguredInferModel.NmsTransformationInfo`]): The NMS transformation info per output.
            #"""
            self._bindings = bindings
            self._inputs = {}
            self._outputs = {}
            self._input_names = input_names
            self._output_names = output_names
            self._nms_infos = nms_infos

        def input(self, name=""):
            """
            Gets an input's InferStream object.

            Args:
                name (str, optional): the name of the input stream. Required in case of multiple inputs.

            Returns:
                :class:`ConfiguredInferModel.Bindings.InferStream` - the input infer stream of the configured infer model.

            Raises:
                :class:`HailoRTNotFoundException` in case a non-existing input is requested or no name is given
                but multiple inputs exist.
            """
            if name == "" and len(self._input_names) == 1:
                name = self._input_names[0]

            if name not in self._inputs:
                with ExceptionWrapper():
                    self._inputs[name] = self.InferStream(self._bindings.input(name))

            return self._inputs[name]

        def output(self, name=""):
            """
            Gets an output's InferStream object.

            Args:
                name (str, optional): the name of the output stream. Required in cae of multiple outputs.

            Returns:
                :class:`ConfiguredInferModel.Bindings.InferStream` - the output infer stream of the configured infer model.

            Raises:
                :class:`HailoRTNotFoundException` in case a non-existing output is requested or no name is given
                but multiple outputs exist.
            """
            if name == "" and len(self._output_names) == 1:
                name = self._output_names[0]

            if name not in self._outputs:
                with ExceptionWrapper():
                    self._outputs[name] = self.InferStream(self._bindings.output(name), self._nms_infos.get(name, None))

            return self._outputs[name]

        def get(self):
            """
            Gets the internal bindings object.

            Returns:
                _bindings (_pyhailort.ConfiguredInferModelBindingsWrapper): the internal bindings object.
            """
            return self._bindings


    def __init__(self, configured_infer_model, infer_model):
        #"""
        #Args:
        #    configured_infer_model (_pyhailort.ConfiguredInferModelWrapper): The internal configured_infer_model object.
        #    infer_model (:class:`InferModel`): The InferModel object.
        #"""
        self._configured_infer_model = configured_infer_model
        self._input_names = infer_model.input_names
        self._output_names = infer_model.output_names
        self._infer_model = infer_model
        self._buffer_guards = deque()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self._configured_infer_model = None

    def activate(self):
        """
        Activates hailo device inner-resources for inference.
        Calling this function is invalid in case scheduler is enabled.

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            self._configured_infer_model.activate()

    def deactivate(self):
        """
        Deactivates hailo device inner-resources for inference.
        Calling this function is invalid in case scheduler is enabled.

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            self._configured_infer_model.deactivate()

    def create_bindings(self, input_buffers=None, output_buffers=None):
        """
        Creates a Bindings object.

        Args:
            input_buffers (dict[str: numpy.array], optional): The input buffers for the Bindings object. Keys are the input names, and values are their corresponding buffers. See :func:`~hailo_platform.pyhailort.pyhailort.ConfiguredInferModel.Bindings.InferStream.get_buffer` for more information.
            output_buffers (dict[str: numpy.array], optional): The output buffers for the Bindings object. Keys are the output names, and values are their corresponding buffers. See :func:`~hailo_platform.pyhailort.pyhailort.ConfiguredInferModel.Bindings.InferStream.get_buffer` for more information.

        Returns:
            :obj:`ConfiguredInferModel.Bindings`: Bindings object

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            bindings_cpp_obj = self._configured_infer_model.create_bindings()

        bindings = self.Bindings(bindings_cpp_obj, self._input_names, self._output_names, self._get_nms_infos())

        if input_buffers:
            for input_name, buffer in input_buffers.items():
                bindings.input(input_name).set_buffer(buffer)

        if output_buffers:
            for output_name, buffer in output_buffers.items():
                bindings.output(output_name).set_buffer(buffer)

        return bindings

    def wait_for_async_ready(self, timeout_ms=1000, frames_count=1):
        """
        The readiness of the model to launch is determined by the ability to push buffers to the asynchronous inference pipeline.
        If the model is ready, the method will return immediately.
        If it's not ready initially, the method will wait for the model to become ready.

        Args:
            timeout_ms (int, optional): Max amount of time to wait until the model is ready in milliseconds.
                Defaults to 1000
            frames_count (int, optional): The number of buffers you intend to infer in the next request.
                Useful for batch inference. Defaults to 1

        Note: Calling this function with frames_count greater than :func:`ConfiguredInferModel.get_async_queue_size` will timeout.

        Raises:
            :class:`HailoRTTimeout` in case the model is not ready in the given timeout.
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            self._configured_infer_model.wait_for_async_ready(timedelta(milliseconds=timeout_ms), frames_count)

    def run(self, bindings, timeout):
        """
        Launches a synchronous inference operation with the provided bindings.

        Args:
            list of bindings (:obj:`ConfiguredInferModel.Bindings`): The bindings for the inputs and outputs of the model.
                A list with a single binding is valid. Multiple bindings are useful for batch inference.
            timeout (int): The timeout in milliseconds.

        Raises:
            :class:`HailoRTException` in case of an error.
            :class:`HailoRTTimeout` in case the job did not finish in the given timeout.
        """
        with ExceptionWrapper():
            job = self.run_async(bindings)
            job.wait(timeout)

    def run_async(self, bindings, callback=None):
        """
        Launches an asynchronous inference operation with the provided bindings.

        Args:
            list of bindings (:obj:`ConfiguredInferModel.Bindings`): The bindings for the inputs and outputs of the model.
                A list with a single binding is valid. Multiple bindings are useful for batch inference.
            callback (Callable, optional): A callback that will be called upon completion of the asynchronous
                inference operation. The function will be called with an info argument
                (:class:`AsyncInferCompletionInfo`) holding the information about the async job. If the async job was
                unsuccessful, the info parameter will hold an exception method that will raise an exception. The
                callback must accept a 'completion_info' keyword argument

        Note:
            As a standard, callbacks should be executed as quickly as possible.
            In case of an error, the pipeline will be shut down.

        Note:
            To ensure the inference pipeline can handle new buffers, it is recommended to first call
                 :func:`ConfiguredInferModel.wait_for_async_ready`.

        Returns:
            AsyncInferJob: The async inference job object.

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        # keep the buffers alive until the job and the callback are completed
        buffers = []
        for b in bindings:
            for name in self._input_names:
                buffers.append(b.input(name).get_buffer())
            for name in self._output_names:
                buffers.append(b.output(name).get_buffer(None))
        self._buffer_guards.append(buffers)

        def callback_wrapper(error_code):
            cpp_cb_exception = ExceptionWrapper.create_exception_from_status(error_code) if error_code else None
            if callback:
                completion_info = AsyncInferCompletionInfo(cpp_cb_exception)
                callback(completion_info=completion_info)

            # remove the buffers - they are no longer needed
            self._buffer_guards.popleft()

        with ExceptionWrapper():
            cpp_job = self._configured_infer_model.run_async(
                [b.get() for b in bindings], callback_wrapper
            )

        job = AsyncInferJob(cpp_job)
        return job

    def set_scheduler_timeout(self, timeout_ms):
        """
        Sets the minimum number of send requests required before the network is considered ready to get run time from the scheduler.
        Sets the maximum time period that may pass before receiving run time from the scheduler.
        This will occur providing at least one send request has been sent, there is no minimum requirement for send
        requests, (e.g. threshold - see :func:`ConfiguredInferModel.set_scheduler_threshold`).

        The new time period will be measured after the previous time the scheduler allocated run time to this network group.
        Using this function is only allowed when scheduling_algorithm is not `HAILO_SCHEDULING_ALGORITHM_NONE`.
        The default timeout is 0ms.

        Args:
            timeout_ms (int): The maximum time to wait for the scheduler to provide run time, in milliseconds.

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            self._configured_infer_model.set_scheduler_timeout(timedelta(milliseconds=timeout_ms))

    def set_scheduler_threshold(self, threshold):
        """
        Sets the minimum number of send requests required before the network is considered ready to get run time from the scheduler.

        Args:
            threshold (int): Threshold in number of frames.

        Using this function is only allowed when scheduling_algorithm is not `HAILO_SCHEDULING_ALGORITHM_NONE`.
        The default threshold is 1.
        If at least one send request has been sent, but the threshold is not reached within a set time period (e.g. timeout - see
        :func:`ConfiguredInferModel.set_scheduler_timeout`), the scheduler will consider the network ready regardless.

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            self._configured_infer_model.set_scheduler_threshold(threshold)

    def set_scheduler_priority(self, priority):
        """
        Sets the priority of the network.
        When the network group scheduler will choose the next network, networks with higher priority will be prioritized in the selection.
        Larger number represents higher priority

        Using this function is only allowed when scheduling_algorithm is not `HAILO_SCHEDULING_ALGORITHM_NONE`.
        The default priority is HAILO_SCHEDULER_PRIORITY_NORMAL.

        Args:
            priority (int): Priority as a number between `HAILO_SCHEDULER_PRIORITY_MIN` - `HAILO_SCHEDULER_PRIORITY_MAX`.

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            self._configured_infer_model.set_scheduler_priority(priority)

    def get_async_queue_size(self):
        """
        Returns Expected of a the number of inferences that can be queued simultaneously for execution.

        Returns:
            size (int): the number of inferences that can be queued simultaneously for execution

        Raises:
            :class:`HailoRTException` in case of an error.
        """
        with ExceptionWrapper():
            return self._configured_infer_model.get_async_queue_size()

    def shutdown(self):
        """
        Shuts the inference down. After calling this method, the model is no longer usable.
        """
        with ExceptionWrapper():
            return self._configured_infer_model.shutdown()

    def _update_cache_offset(self, offset_delta_entries):
        with ExceptionWrapper():
            return self._configured_infer_model.update_cache_offset(offset_delta_entries)

    def _finalize_cache(self):
        with ExceptionWrapper():
            return self._configured_infer_model.finalize_cache()

    def _get_nms_infos(self):
        nms_infos = {}

        for name in self._output_names:
            output = self._infer_model.output(name)
            format_order = output.format.order

            if format_order in (
                FormatOrder.HAILO_NMS_WITH_BYTE_MASK,
                FormatOrder.HAILO_NMS_BY_CLASS,
                FormatOrder.HAILO_NMS_BY_SCORE
            ):
                output_vstream_info = next(
                    filter(
                        lambda item: item.name == name,
                        self._infer_model.hef.get_output_vstream_infos(),
                    )
                )

                if format_order == FormatOrder.HAILO_NMS_WITH_BYTE_MASK:
                    if (len(self._input_names)) != 1:
                        raise HailoRTInvalidHEFException(
                            f"Output format order {format_order} should have 1 input. Number of inputs: {len(self._input_names)}"
                        )

                    input = self._infer_model.input()
                    input_height, input_width = input.shape[:2]
                else:
                    input_height, input_width = -1, -1 # not accessed

                nms_infos[name] = self.NmsTransformationInfo(
                    output.format.order,
                    input_height,
                    input_width,
                    output_vstream_info.nms_shape.number_of_classes,
                    output_vstream_info.nms_shape.max_bboxes_per_class,
                    output.quant_infos[0],
                )

        return nms_infos


class AsyncInferJob:
    """
    Hailo Asynchronous Inference Job Wrapper.
    It holds the result of the inference job (once ready), and provides an async poll method to check the job status.
    """

    MILLISECOND = (1 / 1000)

    def __init__(self, job):
        #"""
        #Args:
        #    job (:obj:`_pyhailort.AsyncInferJob`): The internal AsyncInferJob object.
        #"""
        self._job = job

    def wait(self, timeout_ms):
        """
        Waits for the asynchronous inference job to finish.
        If the async job and its callback have not completed within the given timeout, a HailoRTTimeout exception will be raised.

        Args:
            timeout_ms (int): timeout The maximum time to wait.

        Raises:
            :class:`HailoRTTimeout` in case the job did not finish in the given timeout.
        """
        with ExceptionWrapper():
            self._job.wait(timedelta(milliseconds=timeout_ms))


class VDevice(object):
    """Hailo virtual device representation."""

    def __init__(self, params=None, *, device_ids=None):

        """Create the Hailo virtual device object.

        Args:
            params (:obj:`hailo_platform.pyhailort.pyhailort.VDeviceParams`, optional): VDevice params, call
                :func:`VDevice.create_params` to get default params. Excludes 'device_ids'.
            device_ids (list of str, optional): devices ids to create VDevice from, call :func:`Device.scan` to get
                list of all available devices. Excludes 'params'. Cannot be used together with device_id.
        """
        gc.collect()

        # VDevice __del__ function tries to release self._vdevice.
        # to avoid AttributeError if the __init__ func fails, we set it to None first.
        # https://stackoverflow.com/questions/6409644/is-del-called-on-an-object-that-doesnt-complete-init
        self._vdevice = None

        self._id = "VDevice"
        self._params = params
        self._loaded_network_groups = []
        self._creation_pid = os.getpid()

        self._device_ids = device_ids

        self._open_vdevice()

    def _open_vdevice(self):
        if self._params is None:
            self._params = VDevice.create_params()
        with ExceptionWrapper():
            device_ids = [] if self._device_ids is None else self._device_ids
            self._vdevice = _pyhailort.VDevice.create(self._params, device_ids)

    def __enter__(self):
        return self

    def release(self):
        """Release the allocated resources of the device. This function should be called when working with the device not as context-manager."""
        if self._vdevice is not None:
            self._vdevice.release()
            self._vdevice = None

    def __exit__(self, *args):
        self.release()
        return False

    def __del__(self):
        self.release()

    @staticmethod
    def create_params():
        with ExceptionWrapper():
            return _pyhailort.VDeviceParams.default()

    def configure(self, hef, configure_params_by_name={}):
        """Configures target vdevice from HEF object.

        Args:
            hef (:class:`~hailo_platform.pyhailort.pyhailort.HEF`): HEF to configure the vdevice from
            configure_params_by_name (dict, optional): Maps between each net_group_name to configure_params. If not provided, default params will be applied
        """
        if self._creation_pid != os.getpid():
            raise HailoRTException("VDevice can only be configured from the process it was created in.")
        with ExceptionWrapper():
            configured_ngs_handles = self._vdevice.configure(hef._hef, configure_params_by_name)
        configured_networks = [ConfiguredNetwork(configured_ng_handle) for configured_ng_handle in configured_ngs_handles]
        self._loaded_network_groups.extend(configured_networks)
        return configured_networks

    def get_physical_devices(self):
        """Gets the underlying physical devices.

        Return:
            list of :obj:`~hailo_platform.pyhailort.pyhailort.Device`: The underlying physical devices.
        """
        phys_dev_infos = self.get_physical_devices_ids()
        return [Device(dev_id) for dev_id in phys_dev_infos]

    def get_physical_devices_ids(self):
        """Gets the physical devices ids.

        Return:
            list of :obj:`str`: The underlying physical devices infos.
        """
        with ExceptionWrapper():
            return self._vdevice.get_physical_devices_ids()

    def create_infer_model(self, hef_source, name=""):
        """
        Creates the infer model from an hef.

        Args:
            hef_source (str or bytes): The source from which the HEF object will be created. If the
                source type is `str`, it is treated as a path to an hef file. If the source type is
                `bytes`, it is treated as a buffer. Any other type will raise a ValueError.
            name (str, optional): The string of the model name.

        Returns:
            :obj:`InferModel`: The infer model object.

        Raises:
            :class:`HailoRTException`: In case the infer model creation failed.

        Note:
            create_infer_model must be called from the same process the VDevice is created in,
            otherwise an :class:`HailoRTException` will be raised.

        Note:
            So long as the InferModel object is alive, the VDevice object is alive as well.
        """
        if os.getpid() != self._creation_pid:
            raise HailoRTException("InferModel can be created only from the process VDevice was created in.")

        with ExceptionWrapper():
            if type(hef_source) is bytes:
                infer_model_cpp_obj = self._vdevice.create_infer_model_from_buffer(hef_source, name)
            else:
                infer_model_cpp_obj = self._vdevice.create_infer_model_from_file(hef_source, name)

        infer_model = InferModel(infer_model_cpp_obj, hef_source)
        return infer_model

    @property
    def loaded_network_groups(self):
        """Getter for the property _loaded_network_groups.

        Returns:
            list of :obj:`ConfiguredNetwork`: List of the the configured network groups loaded on the device.
        """
        return self._loaded_network_groups

    @property
    def _loaded_network_group(self):
        if len(self._loaded_network_groups) != 1:
            raise HailoRTException("Access to network group is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0]


class InputVStreamParams(object):
    """Parameters of an input virtual stream (host to device)."""

    @staticmethod
    def make(configured_network, quantized=None, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create input virtual stream params from a configured network group. These params determine the format of the
        data that will be fed into the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized: Unused.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all input virtual streams.
                The default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the data is fed in the same format expected by the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all input virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        if format_type is None:
            format_type = FormatType.AUTO
        if timeout_ms is None:
            timeout_ms = DEFAULT_VSTREAM_TIMEOUT_MS
        if queue_size is None:
            queue_size = DEFAULT_VSTREAM_QUEUE_SIZE
        name = network_name if network_name is not None else ""
        with ExceptionWrapper():
            return configured_network._configured_network.make_input_vstream_params(name, format_type, timeout_ms, queue_size)

    @staticmethod
    def make_from_network_group(configured_network, quantized=None, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create input virtual stream params from a configured network group. These params determine the format of the
        data that will be fed into the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized: Unused.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all input virtual streams.
                The default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the data is fed in the same format expected by the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all input virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        return InputVStreamParams.make(configured_network=configured_network, format_type=format_type, timeout_ms=timeout_ms,
            queue_size=queue_size, network_name=network_name)


class OutputVStreamParams(object):
    """Parameters of an output virtual stream (device to host)."""

    @staticmethod
    def make(configured_network, quantized=None, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create output virtual stream params from a configured network group. These params determine the format of the
        data that will be returned from the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized: Unused.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all output virtual streams.
                The default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the returned data is in the same format returned from the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all output virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        if format_type is None:
            format_type = FormatType.AUTO
        if timeout_ms is None:
            timeout_ms = DEFAULT_VSTREAM_TIMEOUT_MS
        if queue_size is None:
            queue_size = DEFAULT_VSTREAM_QUEUE_SIZE
        name = network_name if network_name is not None else ""
        with ExceptionWrapper():
            return configured_network._configured_network.make_output_vstream_params(name, format_type, timeout_ms, queue_size)

    @staticmethod
    def make_from_network_group(configured_network, quantized=None, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create output virtual stream params from a configured network group. These params determine the format of the
        data that will be returned from the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized: Unused.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all output virtual streams.
                The default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the returned data is in the same format returned from the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all output virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        return OutputVStreamParams.make(configured_network=configured_network, format_type=format_type, timeout_ms=timeout_ms,
            queue_size=queue_size, network_name=network_name)

    @staticmethod
    def make_groups(configured_network, quantized=None, format_type=None, timeout_ms=None, queue_size=None):
        """Create output virtual stream params from a configured network group. These params determine the format of the
        data that will be returned from the network group. The params groups are splitted with respect to their underlying streams for multi process usges.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized: Unused.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all output virtual streams.
                The default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the returned data is in the same format returned from the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all output virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.

        Returns:
            list of dicts: Each element in the list represent a group of params, where the keys are the vstreams names, and the values are the
            params. The params groups are splitted with respect to their underlying streams for multi process usges.
        """
        all_params = OutputVStreamParams.make(configured_network=configured_network, format_type=format_type, timeout_ms=timeout_ms, queue_size=queue_size)
        low_level_streams_names = [stream_info.name for stream_info in configured_network.get_output_stream_infos()]
        stream_name_to_vstream_names = {stream_name: configured_network.get_vstream_names_from_stream_name(stream_name) for stream_name in low_level_streams_names}
        results = []
        for low_level_stream_name, vstream_names in stream_name_to_vstream_names.items():
            params_group = {}
            for vstream_name in vstream_names:
                # Vstreams that were already seen should not be added to another params_group
                if all_params[vstream_name] is not None:
                    params_group[vstream_name] = all_params[vstream_name]
                    all_params[vstream_name] = None
            if 0 < len(params_group):
                results.append(params_group)
        return results


class InputVStream(object):
    """Represents a single virtual stream in the host to device direction."""

    def __init__(self, send_object):
        self._send_object = send_object
        self._input_dtype = self._send_object.dtype

    @property
    def shape(self):
        return self._send_object.shape
    
    @property
    def dtype(self):
        return self._send_object.dtype

    @property
    def name(self):
        return self._send_object.info.name

    @property
    def network_name(self):
        return self._send_object.info.network_name

    def send(self, input_data):
        """Send frames to inference.

        Args:
            input_data (:obj:`numpy.ndarray`): Data to run inference on.
        """

        if input_data.dtype != self._input_dtype:
            input_data = input_data.astype(self._input_dtype)

        if not input_data.flags.c_contiguous:
            logger = default_logger()
            logger.warning("Warning - Converting input numpy array to be C_CONTIGUOUS")
            input_data = numpy.asarray(input_data, order='C')

        batch_number = 0
        batch_size = 1
        while batch_number < input_data.shape[0]:
            data = input_data[batch_number:batch_number + batch_size]
            with ExceptionWrapper():
                self._send_object.send(data)
            batch_number += batch_size

    def flush(self):
        """Blocks until there are no buffers in the input VStream pipeline."""
        with ExceptionWrapper():
            self._send_object.flush()

    @property
    def info(self):
        with ExceptionWrapper():
            return self._send_object.info

class InputVStreams(object):
    """Input vstreams pipelines that allows to send data, to be used as a context manager."""

    def __init__(self, configured_network, input_vstreams_params):
        """Constructor for the InputVStreams class.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which the pipeline is created.
            input_vstreams_params (dict from str to :class:`InputVStreamParams`): Params for the input vstreams in the pipeline.
        """
        self._configured_network = configured_network
        self._input_vstreams_params = input_vstreams_params
        self._vstreams = {}

    def __enter__(self):
        self._input_vstreams_holder = self._configured_network._create_input_vstreams(self._input_vstreams_params)
        self._input_vstreams_holder.__enter__()
        for name, vstream in self._input_vstreams_holder.get_all_inputs().items():
            self._vstreams[name] = InputVStream(vstream)
        return self

    def get(self, name=None):
        """Return a single input vstream by its name.
        
        Args:
            name (str): The vstream name. If name=None and there is a single input vstream, that single (:class:`InputVStream`) will be returned.
                Otherwise, if name=None and there are multiple input vstreams, an exception will be thrown.

        Returns:
            :class:`InputVStream`: The (:class:`InputVStream`) that corresponds to the given name.
        """
        if name is None:
            if len(self._vstreams) != 1:
                raise HailoRTException("There is no single input vStream. You must give a name")
            name = list(self._vstreams.keys())[0]
        return self._vstreams[name]

    def clear(self):
        """Clears the vstreams' pipeline buffers."""
        with ExceptionWrapper():
            self._input_vstreams_holder.clear()

    def __exit__(self, *args):
        self._input_vstreams_holder.__exit__(*args)
        return False

    def __iter__(self):
        return iter(self._vstreams.values())



class OutputLayerUtils(object):
    def __init__(self, output_vstream_infos, vstream_name, pipeline, net_group_name=""):
        self._output_vstream_infos = output_vstream_infos
        self._vstream_info = self._get_vstream_info(net_group_name, vstream_name)

        if isinstance(pipeline, (_pyhailort.InferVStreams)):
            self._user_buffer_format = pipeline.get_user_buffer_format(vstream_name)
            self._output_shape = pipeline.get_shape(vstream_name)
        else:
            self._user_buffer_format = pipeline.get_user_buffer_format()
            self._output_shape = pipeline.shape

        self._is_nms = (self._user_buffer_format.order in [FormatOrder.HAILO_NMS_BY_CLASS, FormatOrder.HAILO_NMS_BY_SCORE])

        if self._is_nms:
            self._quantized_empty_bbox = numpy.asarray([0] * BBOX_PARAMS, dtype=self.output_dtype)
            if self.output_dtype == numpy.float32:
                HailoRTTransformUtils.dequantize_output_buffer_in_place(self._quantized_empty_bbox, self.output_dtype,
                    BBOX_PARAMS, self._vstream_info.quant_info)

    @property
    def output_dtype(self):
        return _pyhailort.get_dtype(self._user_buffer_format.type)

    @property
    def output_order(self):
        return self._user_buffer_format.order

    @property
    def output_shape(self):
        return self._output_shape

    @property
    def vstream_info(self):
        return self._vstream_info

    @property
    def output_tensor_info(self):
        return self.output_shape, self.output_dtype

    @property
    def is_nms(self):
        return self._is_nms
    
    @property
    def quantized_empty_bbox(self):
        if not self.is_nms:
            raise HailoRTException("Requested NMS info for non-NMS layer")
        return self._quantized_empty_bbox

    def _get_vstream_info(self, net_group_name, vstream_name):
        for info in self._output_vstream_infos:
            if info.name == vstream_name:
                return info
        raise HailoRTException("No vstream matches the given name {}".format(vstream_name))

    @property
    def tf_nms_fomrat_shape(self):
        # TODO: HRT-11726 - Combine is_nms for HAILO_NMS and NMS_WITH_BYTE_MASK
        if not self.is_nms and not self.output_order == FormatOrder.HAILO_NMS_WITH_BYTE_MASK:
            raise HailoRTException("Requested NMS info for non-NMS layer")
        nms_shape = self._vstream_info.nms_shape
        return [nms_shape.number_of_classes, BBOX_PARAMS,
                nms_shape.max_bboxes_per_class]

class OutputVStream(object):
    """Represents a single output virtual stream in the device to host direction."""

    def __init__(self, configured_network, recv_object, name, tf_nms_format=False, net_group_name=""):
        self._recv_object = recv_object
        output_vstream_infos = configured_network.get_output_vstream_infos()
        self._output_layer_utils = OutputLayerUtils(output_vstream_infos, name, self._recv_object, net_group_name)
        self._output_dtype = self._output_layer_utils.output_dtype
        self._vstream_info = self._output_layer_utils._vstream_info
        self._output_tensor_info = self._output_layer_utils.output_tensor_info
        self._is_nms = self._output_layer_utils.is_nms
        if self._is_nms:
            self._quantized_empty_bbox = self._output_layer_utils.quantized_empty_bbox
        self._tf_nms_format = tf_nms_format
        self._input_stream_infos = configured_network.get_input_stream_infos()

    @property
    def output_order(self):
        return self._output_layer_utils.output_order

    @property
    def shape(self):
        return self._recv_object.shape

    @property
    def dtype(self):
        return self._recv_object.dtype

    @property
    def name(self):
        return self._vstream_info.name

    @property
    def network_name(self):
        return self._vstream_info.network_name

    def recv(self):
        """Receive frames after inference.

        Returns:
            :obj:`numpy.ndarray`: The output of the inference for a single frame. The returned
            tensor does not include the batch dimension.
            In case of nms output and tf_nms_format=False, returns list of :obj:`numpy.ndarray`.
        """
        result_array = None
        with ExceptionWrapper():
            result_array = self._recv_object.recv()

        if self.output_order == FormatOrder.HAILO_NMS_WITH_BYTE_MASK:
            if len(self._input_stream_infos) != 1:
                raise HailoRTInvalidHEFException(
                    f"Output format HAILO_NMS_WITH_BYTE_MASK should have 1 input. Number of inputs: {len(self._input_stream_infos)}"
                )
            if self._tf_nms_format:
                nms_shape = self._vstream_info.nms_shape
                input_height = self._input_stream_infos[0].shape[0]
                input_width = self._input_stream_infos[0].shape[1]

                converted_output_buffer = numpy.empty(
                    [
                        nms_shape.max_bboxes_per_class,
                        (input_height * input_width + BBOX_WITH_MASK_PARAMS),
                    ],
                    dtype=self._output_dtype,
                )

                res = HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_tf_format_single_frame(
                    result_array,
                    converted_output_buffer,
                    nms_shape.number_of_classes,
                    nms_shape.max_bboxes_per_class,
                    input_height,
                    input_width,
                )
            else:
                res = HailoRTTransformUtils._output_raw_buffer_to_nms_with_byte_mask_hailo_format_single_frame(result_array)
            return res

        if self._is_nms:
            nms_shape = self._vstream_info.nms_shape
            if self._tf_nms_format:
                nms_results_tesnor = result_array
                # We create the tf_format buffer with reversed width/features for preformance optimization
                shape = self._output_layer_utils.tf_nms_fomrat_shape
                result_array = numpy.empty([shape[0], shape[2], shape[1]], dtype=self._output_dtype)
                HailoRTTransformUtils.output_raw_buffer_to_nms_tf_format_single_frame(nms_results_tesnor, result_array,
                    nms_shape.number_of_classes,
                    nms_shape.max_bboxes_per_class, self._quantized_empty_bbox)
                result_array = numpy.swapaxes(result_array, 1, 2)
            else:
                result_array = HailoRTTransformUtils.output_raw_buffer_to_nms_format_single_frame(result_array,
                    nms_shape.number_of_classes)
        return result_array

    @property
    def info(self):
        with ExceptionWrapper():
            return self._recv_object.info

    def set_nms_score_threshold(self, threshold):
        """Set NMS score threshold, used for filtering out candidates. Any box with score<TH is suppressed.

        Args:
            threshold (float): NMS score threshold to set.

        Note:
            This function will fail in cases where the output vstream has no NMS operations on the CPU.
        """
        return self._recv_object.set_nms_score_threshold(threshold)

    def set_nms_iou_threshold(self, threshold):
        """Set NMS intersection over union overlap Threshold,
            used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.

        Args:
            threshold (float): NMS IoU threshold to set.

        Note:
            This function will fail in cases where the output vstream has no NMS operations on the CPU.
        """
        return self._recv_object.set_nms_iou_threshold(threshold)

    def set_nms_max_proposals_per_class(self, max_proposals_per_class):
        """Set a limit for the maximum number of boxes per class.

        Args:
            max_proposals_per_class (int): NMS max proposals per class to set.

        Note:
            This function will fail in cases where the output vstream has no NMS operations on the CPU.
        """
        return self._recv_object.set_nms_max_proposals_per_class(max_proposals_per_class)

    def set_nms_max_accumulated_mask_size(self, max_accumulated_mask_size):
        """Set maximum accumulated mask size for all the detections in a frame.
            Used in order to change the output buffer frame size,
            in cases where the output buffer is too small for all the segmentation detections.

        Args:
            max_accumulated_mask_size (int): NMS max accumulated mask size.

        Note:
            This function must be called before starting inference!
            This function will fail in cases where there is no output with NMS operations on the CPU.
        """
        return self._recv_object.set_nms_max_accumulated_mask_size(max_accumulated_mask_size)


class OutputVStreams(object):
    """Output virtual streams pipelines that allows to receive data, to be used as a context manager."""

    def __init__(self, configured_network, output_vstreams_params, tf_nms_format=False):
        """Constructor for the OutputVStreams class.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the pipeline is created.
            output_vstreams_params (dict from str to :class:`OutputVStreamParams`): Params for the
                output vstreams in the pipeline.
            tf_nms_format (bool, optional): indicates whether the returned nms outputs should be in
                Hailo format or TensorFlow format. Default is False (using Hailo format).

                * Hailo format -- list of :obj:`numpy.ndarray`. Each element represents th
                  detections (bboxes) for the class, and its shape is
                  ``[number_of_detections, BBOX_PARAMS]``
                * TensorFlow format -- :obj:`numpy.ndarray` of shape
                  ``[class_count, BBOX_PARAMS, detections_count]`` padded with empty bboxes.
        """
        self._configured_network = configured_network
        self._net_group_name = configured_network.name
        self._output_vstreams_params = output_vstreams_params
        self._output_tensor_info = {}
        self._tf_nms_format = tf_nms_format
        self._vstreams = {}

    def __enter__(self):
        self._output_vstreams_holder = self._configured_network._create_output_vstreams(self._output_vstreams_params)
        self._output_vstreams_holder.__enter__()
        for name, vstream in self._output_vstreams_holder.get_all_outputs().items():
            self._vstreams[name] = OutputVStream(self._configured_network, vstream, name,
                tf_nms_format=self._tf_nms_format, net_group_name=self._net_group_name)
        return self

    def get(self, name=None):
        """Return a single output vstream by its name.
        
        Args:
            name (str): The vstream name. If name=None and there is a single output vstream, that single (:class:`OutputVStream`) will be returned.
                Otherwise, if name=None and there are multiple output vstreams, an exception will be thrown.

        Returns:
            :class:`OutputVStream`: The (:class:`OutputVStream`) that corresponds to the given name.
        """
        if name is None:
            if len(self._vstreams) != 1:
                raise HailoRTException("There is no single output vStream. You must give a name")
            name = list(self._vstreams.keys())[0]
        return self._vstreams[name]

    def clear(self):
        """Clears the vstreams' pipeline buffers."""
        with ExceptionWrapper():
            self._output_vstreams_holder.clear()

    def __exit__(self, *args):
        self._output_vstreams_holder.__exit__(*args)
        return False

    def __iter__(self):
        return iter(self._vstreams.values())


class HailoSession(object):
    def __init__(self, session):
        self._session = session

    @staticmethod
    def connect(port, device_id=""):
        with ExceptionWrapper():
            session = _pyhailort.Session.connect(port, device_id)
            return HailoSession(session)

    def write(self, input_data):
        with ExceptionWrapper():
            return self._session.write(input_data)

    def read(self):
        with ExceptionWrapper():
            return self._session.read()

    def close(self):
        with ExceptionWrapper():
            return self._session.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
        return False


class HailoSessionListener(object):
    def __init__(self, port, device_id=""):
        self._device_id = device_id
        self._port = port
        with ExceptionWrapper():
            self._listener = _pyhailort.SessionListener.create(self._port, self._device_id)

    def accept(self):
        with ExceptionWrapper():
            assert self._listener is not None
            session = self._listener.accept()
            return HailoSession(session=session)


class LLMGeneratorCompletion:
    """
    Generator completion object for streaming LLM text generation.

    This class provides an iterator interface for receiving generated tokens one by one
    from a Large Language Model. It is created by the ``LLM.generate()`` method and should
    be used within a context manager for proper resource cleanup.

    Example::

        with llm.generate(prompt="Hello", max_generated_tokens=50) as gen_completion:
            for token in gen_completion:
                print(token, end='', flush=True)
    """

    def __init__(self, generator_completion):
        """
        Initialize the generator completion wrapper.

        Args:
            generator_completion: Internal generator completion object.
        """
        self._generator_completion = generator_completion

    def release(self):
        """
        Release internal resources.

        This method is called automatically when exiting the context manager.
        Manual calls are needed only in case context manager is not used.
        """
        if self._generator_completion is not None:
            self._generator_completion.release()
            self._generator_completion = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()
        return False

    @property
    def generation_status(self):
        """
        Get the current generation status.

        Returns:
            LLMGeneratorCompletionStatus: Current status of the generation process.
        """
        with ExceptionWrapper():
            return self._generator_completion.generation_status()

    def read(self, timeout_ms=10000):
        with ExceptionWrapper():
            return self._generator_completion.read(timeout_ms)

    def read_all(self, timeout_ms=10000):
        """
        Read all remaining tokens and return complete response.

        This method blocks until generation is complete or timeout occurs.

        Args:
            timeout_ms (int, optional): Timeout in milliseconds. Defaults to 10000.

        Returns:
            str: Complete generated response as a single string.

        Example::

            full_response = gen_completion.read_all()
            print(f"Complete response: {full_response}")
        """
        with ExceptionWrapper():
            return self._generator_completion.read_all(timeout_ms)

    def __iter__(self):
        """Iterator protocol support for streaming tokens."""
        return self

    def __next__(self):
        """
        Get next token in iterator protocol.

        Returns:
            str: Next generated token.

        Raises:
            StopIteration: When generation is complete.
        """
        return self._get_next_token()

    def _get_next_token(self):
        if self.generation_status != _pyhailort.LLMGeneratorCompletionStatus.GENERATING:
            raise StopIteration("Generation complete")
        return self.read()


class LLM:
    """
    Large Language Model inference interface for Hailo devices.

    This class provides a high-level Python interface for running Large Language Models
    on Hailo hardware. It supports both streaming and non-streaming text generation,
    context management, and various generation parameters.

    Features:
        - Streaming and non-streaming text generation
        - Structured prompts (chat format) and raw text prompts
        - Context management for multi-turn conversations
        - Flexible generation parameters (temperature, top_p, etc.)
        - LoRA (Low-Rank Adaptation) support for fine-tuned models
        - Built-in tokenization utilities

    Example::

        # Basic usage
        with VDevice() as vd:
            with LLM(vd, "model.hef", optimize_memory_on_device=False) as llm:
                response = llm.generate_all("Hello, how are you?")
                print(response)

        # Streaming generation
        with VDevice() as vd:
            with LLM(vd, "model.hef", optimize_memory_on_device=False) as llm:
                prompt = [{"role": "user", "content": "Tell me a joke"}]
                with llm.generate(prompt, temperature=0.8) as gen:
                    for token in gen:
                        print(token, end='', flush=True)

    Note:
        - The VDevice must remain active for the lifetime of the LLM instance.
        - Using this class within a context manager (``with`` statement) is recommended to ensure proper resource cleanup.
    """

    def __init__(self, vdevice, model_path, lora_name="", optimize_memory_on_device=False):
        """
        Initialize the LLM with a Hailo device and model file.

        Args:
            vdevice (VDevice): An active VDevice instance managing Hailo hardware.
            model_path (str): Path to the compiled HEF (Hailo Executable Format) file.
            lora_name (str, optional): Name of LoRA adapter to load. Defaults to "".
            optimize_memory_on_device (bool, optional): Whether to optimize memory usage on device by enabling client-side tokenization. Defaults to False.

        Example::

            with VDevice() as vd:
                llm = LLM(vd, "models/qwen-2.5-1.5b-instruct.hef", lora_name="chat_adapter", optimize_memory_on_device=True)
        """
        self._llm = _pyhailort.LLMWrapper.create(vdevice._vdevice, model_path, lora_name, optimize_memory_on_device)

    def release(self):
        """
        Release internal resources and cleanup.

        This method is called automatically when exiting the context manager.
        Manual calls are needed only in case context manager is not used.
        """
        if self._llm is not None:
            self._llm.release()
            self._llm = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()
        return False

    @staticmethod
    def _fill_generator_params(gen_params, temperature, top_p, top_k, frequency_penalty, max_generated_tokens, do_sample, seed):
        if temperature is not None:
            gen_params.temperature = temperature
        if top_p is not None:
            gen_params.top_p = top_p
        if top_k is not None:
            gen_params.top_k = top_k
        if frequency_penalty is not None:
            gen_params.frequency_penalty = frequency_penalty
        if max_generated_tokens is not None:
            gen_params.max_generated_tokens = max_generated_tokens
        if do_sample is not None:
            gen_params.do_sample = do_sample
        if seed is not None:
            gen_params.seed = seed
        return gen_params

    @staticmethod
    def _convert_messages_json(prompt):
        if not isinstance(prompt, list):
            raise HailoRTException("prompt must be a list of dicts in chat format")
        for item in prompt:
            if not isinstance(item, dict):
                raise HailoRTException("prompt must be a list of dicts")
        # Convert list of dictionaries to list of JSON strings
        return [json.dumps(item) for item in prompt]

    def generate(self, prompt, temperature=None, top_p=None, top_k=None, frequency_penalty=None, max_generated_tokens=None, do_sample=None, seed=None):
        """
        Generate text in streaming mode, yielding tokens as they are produced.

        This method returns a context manager that provides an iterator for receiving
        generated tokens one by one. This is ideal for interactive applications where
        you want to display text as it's being generated.

        Args:
            prompt (str or list): Input prompt. Can be:
                - str: Raw text prompt, to be inserted to the model as is.
                - list: Structured chat format [{"role": "user", "content": "..."}]
            temperature (float, optional): Sampling temperature (0.0-2.0).
                Lower values = more deterministic, higher = more creative. Default varies by model.
            top_p (float, optional): Nucleus sampling threshold (0.0-1.0).
                Only consider tokens with cumulative probability up to this value.
            top_k (int, optional): Top-k sampling limit. Only consider top-k most likely tokens.
            frequency_penalty (float, optional): Repetition penalty. Positive values
                discourage repetition.
            max_generated_tokens (int, optional): Maximum number of tokens to generate.
                If None, uses model default.
            do_sample (bool, optional): Whether to use sampling (True) or greedy decoding (False).
            seed (int, optional): Random seed for reproducible outputs.
                If None, uses random seed.

        Returns:
            :class:`LLMGeneratorCompletion`: Generator completion object that can be iterated
            to receive tokens one by one.

        Example::

            # Streaming with structured prompt
            prompt = [{"role": "user", "content": "Tell me a story"}]
            with llm.generate(prompt, temperature=0.8, max_generated_tokens=100) as gen:
                for token in gen:
                    print(token, end='', flush=True)

            # Streaming with raw prompt
            with llm.generate("Once upon a time", temperature=0.7) as gen:
                for token in gen:
                    print(token, end='', flush=True)

        Note:
            The conversation context is automatically maintained between calls.
            Use ``clear_context()`` to reset the conversation history.
        """
        # Validate that prompt is either string or a list of JSON-like dicts
        if not isinstance(prompt, str):
            prompt = LLM._convert_messages_json(prompt)

        with ExceptionWrapper():
            generation_params = self._llm.create_generator_params()
            generation_params = LLM._fill_generator_params(generation_params, temperature, top_p, top_k, frequency_penalty, max_generated_tokens, do_sample, seed)
            return LLMGeneratorCompletion(self._llm.generate(generation_params, prompt))

    def generate_all(self, prompt, temperature=None, top_p=None, top_k=None, frequency_penalty=None, max_generated_tokens=None, do_sample=None,
        seed=None, timeout_ms=600000):
        """
        Generate complete text response in non-streaming mode.

        This method generates the complete response and returns it as a single string.
        Use this when you need the full response before proceeding, or for batch processing
        where streaming is not required.

        Args:
            prompt (str or list): Input prompt. Can be:
                - str: Raw text prompt, to be inserted to the model as is.
                - list: Structured chat format [{"role": "user", "content": "..."}]
            temperature (float, optional): Sampling temperature (0.0-2.0).
                Lower values = more deterministic, higher = more creative.
            top_p (float, optional): Nucleus sampling threshold (0.0-1.0).
                Only consider tokens with cumulative probability up to this value.
            top_k (int, optional): Top-k sampling limit. Only consider top-k most likely tokens.
            frequency_penalty (float, optional): Repetition penalty. Positive values
                discourage repetition.
            max_generated_tokens (int, optional): Maximum number of tokens to generate.
                If None, uses model default.
            do_sample (bool, optional): Whether to use sampling (True) or greedy decoding (False).
            seed (int, optional): Random seed for reproducible outputs.
                If None, uses random seed.
            timeout_ms (int, optional): Timeout in milliseconds.

        Returns:
            str: Complete generated response as a single string.

        Example::

            # Simple text generation
            response = llm.generate_all("Explain quantum physics")
            print(response)

            # Chat-style interaction
            prompt = [
                {"role": "system", "content": "You are a helpful assistant."},
                {"role": "user", "content": "What is the capital of France?"}
            ]
            print(llm.generate_all(prompt, temperature=0.1))

            # With specific parameters
            response = llm.generate_all(
                "1+1=",
                temperature=0.8,
                max_generated_tokens=1
            )

        Note:
            The conversation context is automatically maintained between calls.
            Use ``clear_context()`` to reset the conversation history.
        """
        with self.generate(prompt, temperature, top_p, top_k, frequency_penalty, max_generated_tokens, do_sample, seed) as gen_completion:
            return gen_completion.read_all(timeout_ms)

    def tokenize(self, text):
        """
        Tokenize input text using the model's tokenizer.

        This method converts text into a list of token IDs that the model uses internally.
        Useful for understanding how the model processes input, for debugging and
        for understanding how many tokens are needed for a given prompt.

        Args:
            text (str): Text to tokenize.

        Returns:
            list: List of token IDs (integers) representing the input text.

        Example::

            tokens = llm.tokenize("Hello, world!")
            print(f"Token IDs: {tokens}")
            print(f"Number of tokens: {len(tokens)}")
        """
        with ExceptionWrapper():
            return self._llm.tokenize(text)

    def get_context_usage_size(self):
        """
        Get the context usage size of the LLM model.

        This method returns the total number of tokens currently stored in the model's context,
        including both input tokens and generated tokens from previous interactions.
        The context usage includes the conversation history and any tokens that have been
        processed but not yet cleared by clear_context().

        Returns:
            int: Current number of tokens in the context.
        """
        with ExceptionWrapper():
            return self._llm.get_context_usage_size()

    def max_context_capacity(self):
        """
        Obtain the maximum context capacity of the LLM model.

        This method returns the maximum number of tokens that can be stored in the model's context.
        This value is determined in the model compilation,
        represents the upper limit for context storage, and is not configurable.

        Returns:
            int: Maximum number of tokens that can be stored in the context.

        Example::

            max_capacity = llm.max_context_capacity()
            current_usage = llm.get_context_usage_size()
            print(f"Context usage: {current_usage}/{max_capacity}")
        """
        with ExceptionWrapper():
            return self._llm.max_context_capacity()

    def clear_context(self):
        """
        Clear the conversation context/history.

        This method resets the model's memory of previous interactions, starting
        fresh for the next generation. Call this between separate conversations
        or when you want to start a new topic without prior context.

        Example::

            # First conversation
            response1 = llm.generate_all("My name is Alice")
            response2 = llm.generate_all("What's my name?")  # Will remember "Alice"

            # Clear context and start fresh
            llm.clear_context()
            response3 = llm.generate_all("What's my name?")  # Won't remember "Alice"

        Note:
            This operation is irreversible. Once context is cleared, previous
            conversation history cannot be recovered.
        """
        with ExceptionWrapper():
            self._llm.clear_context()

    def get_generation_recovery_sequence(self):
        """
        Get the current generation recovery sequence.

        Recovery sequences are used to handle error conditions during generation,
        or for when a generation is interrupted (e.g. by reaching max_generated_tokens).
        This is an advanced feature for error handling and debugging.

        Returns:
            str: Recovery sequence string, to be inserted to the model when a generation is interrupted.

        Note:
            This is an advanced feature. Most users won't need to use this method.
        """
        with ExceptionWrapper():
            return self._llm.get_generation_recovery_sequence()

    def set_generation_recovery_sequence(self, sequence):
        """
        Set a custom generation recovery sequence.

        Recovery sequences are used to handle error conditions during generation,
        or for when a generation is interrupted (e.g. by reaching max_generated_tokens).
        This is an advanced feature for error handling and debugging.

        Args:
            sequence (str): Recovery sequence string, to be inserted to the model when a generation is interrupted.

        Note:
            This is an advanced feature. Most users won't need to use this method.
        """
        with ExceptionWrapper():
            self._llm.set_generation_recovery_sequence(sequence)

    def prompt_template(self):
        """
        Get the model's prompt template format.

        Different models use different prompt templates (chat template) for translating
        structured prompts (list of dicts) to raw text prompts.
        This method returns information about the expected format for this model.

        Returns:
            str: Prompt template as a string (Jinja2 template).

        Example::

            template = llm.prompt_template()
            print(f"Model expects format: {template}")

        Note:
            When using structured prompts (list of dicts), the template is applied
            automatically. This method is useful when constructing raw text prompts.
        """
        with ExceptionWrapper():
            return self._llm.prompt_template()

    def set_stop_tokens(self, stop_tokens):
        """
        Set custom stop tokens for generation.

        Stop tokens are sequences that, when generated, will cause the model
        to stop generating additional text. This is useful for controlling
        response length and format.

        A stop token can be a string that represents multiple tokens. In this case,
        the generation will stop when the exact string is generated as a sequence.
        All stop tokens are checked after each generated token. Setting too many
        can affect performance. Setting stop tokens overrides current stop tokens.
        Setting empty list will remove all stop tokens.

        Args:
            stop_tokens (list): List of strings that when generated will stop the generation.
        """
        with ExceptionWrapper():
            self._llm.set_stop_tokens(stop_tokens)

    def get_stop_tokens(self):
        """
        Get the currently configured stop tokens.

        Returns:
            list: List of strings, where each string is a current stop token.

        Example::

            current_stops = llm.get_stop_tokens()
            print(f"Current stop tokens: {current_stops}")
        """
        with ExceptionWrapper():
            return self._llm.get_stop_tokens()

    def save_context(self):
        """
        Save the current context of the LLM as a binary blob.

        This method saves the current conversation context (including all previous
        interactions) as a binary blob that can be used to restore the context later
        using load_context(). This is useful for saving conversation state between
        sessions or for implementing context switching.

        Returns:
            bytes: Binary blob containing the saved context data.

        Example::

            # Save context after a conversation
            context_data = llm.save_context()

            # Save to file for later use
            with open("conversation_context.bin", "wb") as f:
                f.write(context_data)

        Note:
            - The context is unique for a specific LLM model
            - Trying to load a context from a different model will result in an error
            - The context includes all conversation history up to this point
        """
        with ExceptionWrapper():
            return bytes(self._llm.save_context())

    def load_context(self, context_data):
        """
        Load a previously saved context into the LLM.

        This method restores a conversation context that was previously saved using
        save_context(). This allows to continue the conversation from where it
        left off, or to switch between different conversation contexts.

        Args:
            context_data (bytes): Binary blob containing the saved context data.

        Example::

            # Load context from file
            with open("conversation_context.bin", "rb") as f:
                context_data = f.read()

            # Restore the conversation context
            llm.load_context(context_data)

            # Continue the conversation
            response = llm.generate_all("What were we talking about?")

        Note:
            - The context must be from the same LLM model
            - Loading a context will replace the current context completely
            - This operation will fail if the context is from a different model
        """
        with ExceptionWrapper():
            self._llm.load_context(list(context_data))


class VLM:
    """
    Vision Language Model inference interface for Hailo devices.

    This class provides a high-level Python interface for running Vision Language Models
    on Hailo hardware. It supports both streaming and non-streaming text generation,
    context management, and various generation parameters.

    Features:
        - Streaming and non-streaming text generation, supporting multiple (or 0) images per generation
        - Structured prompts (chat format) and raw text prompts
        - Context management for multi-turn conversations
        - Flexible generation parameters (temperature, top_p, etc.)
        - Built-in tokenization utilities

    Example::

        # Basic usage
        with VDevice() as vd:
            with VLM(vd, "model.hef", optimize_memory_on_device=False) as vlm:
                response = vlm.generate_all(prompt="Hello, how are you?", frames=[])
                print(response)

        # Streaming generation
        with VDevice() as vd:
            with VLM(vd, "model.hef", optimize_memory_on_device=False) as vlm:
                prompt = [{"role": "user", "content": [{"type": "text", "text": "Describe this image"}, {"type": "image"}]}]
                with vlm.generate(prompt, frames=[frame_numpy_array], temperature=0.8) as gen:
                    for token in gen:
                        print(token, end='', flush=True)

    Note:
        - The VDevice must remain active for the lifetime of the VLM instance.
        - Using this class within a context manager (``with`` statement) is recommended to ensure proper resource cleanup.
        - The given frames must be in the same order, shape, and dtype as the input frame of the model.
    """

    def __init__(self, vdevice, model_path, optimize_memory_on_device=False):
        """
        Initialize the VLM with a Hailo device and model file.

        Args:
            vdevice (VDevice): An active VDevice instance managing Hailo hardware.
            model_path (str): Path to the compiled HEF (Hailo Executable Format) file.
            optimize_memory_on_device (bool, optional): Whether to optimize memory usage on device by enabling client-side tokenization. Defaults to False.

        Example::

            with VDevice() as vd:
                vlm = VLM(vd, "models/qwen2-vl-2b-instruct.hef", optimize_memory_on_device=True)
        """
        self._vlm = _pyhailort.VLMWrapper.create(vdevice._vdevice, model_path, optimize_memory_on_device)

    def release(self):
        """
        Release internal resources and cleanup.

        This method is called automatically when exiting the context manager.
        Manual calls are needed only in case context manager is not used.
        """
        if self._vlm is not None:
            self._vlm.release()
            self._vlm = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()
        return False

    @staticmethod
    def _ensure_frames(frames):
        if not isinstance(frames, list):
            raise HailoRTException("frames must be a list of numpy arrays")
        for arr in frames:
            if not hasattr(arr, 'dtype') or not hasattr(arr, 'nbytes'):
                raise HailoRTException("each frame must be a numpy array")

    def generate(self, prompt, frames, temperature=None, top_p=None, top_k=None, frequency_penalty=None,
        max_generated_tokens=None, do_sample=None, seed=None):
        """
        Generate text in streaming mode with vision input, yielding tokens as they are produced.

        This method returns a context manager that provides an iterator for receiving
        generated tokens one by one. This is ideal for interactive applications where
        you want to display text as it's being generated.

        Args:
            prompt (str or list): Input prompt. Can be:
                - str: Raw text prompt, to be inserted to the model as is.
                - list: Structured chat format with or without vision content [{"role": "user", "content": [{"type": "text", "text": "..."}, {"type": "image"}]}]
            frames (list): List of numpy arrays representing input images/frames. Empty list means no vision input. Must match model's expected input format.
            temperature (float, optional): Sampling temperature (0.0-2.0).
                Lower values = more deterministic, higher = more creative. Default varies by model.
            top_p (float, optional): Nucleus sampling threshold (0.0-1.0).
                Only consider tokens with cumulative probability up to this value.
            top_k (int, optional): Top-k sampling limit. Only consider top-k most likely tokens.
            frequency_penalty (float, optional): Repetition penalty. Positive values
                discourage repetition.
            max_generated_tokens (int, optional): Maximum number of tokens to generate.
                If None, uses model default.
            do_sample (bool, optional): Whether to use sampling (True) or greedy decoding (False).
            seed (int, optional): Random seed for reproducible outputs.
                If None, uses random seed.

        Returns:
            :class:`LLMGeneratorCompletion`: Generator completion object that can be iterated
            to receive tokens one by one.

        Example::

            # Streaming with structured prompt and image
            prompt = [{"role": "user", "content": [{"type": "text", "text": "Describe this image"}, {"type": "image"}]}]
            with vlm.generate(prompt, frames=[image_array], temperature=0.7) as gen:
                for token in gen:
                    print(token, end='', flush=True)

        Note:
            The conversation context is automatically maintained between calls.
            Use ``clear_context()`` to reset the conversation history.
        """
        # Validate that prompt is either string or a list of JSON-like dicts
        if not isinstance(prompt, str):
            prompt = LLM._convert_messages_json(prompt)
        VLM._ensure_frames(frames)

        with ExceptionWrapper():
            generation_params = self._vlm.create_generator_params()
            generation_params = LLM._fill_generator_params(generation_params, temperature, top_p, top_k, frequency_penalty, max_generated_tokens, do_sample, seed)
            return LLMGeneratorCompletion(self._vlm.generate(generation_params, prompt, frames))

    def generate_all(self, prompt, frames, temperature=None, top_p=None, top_k=None, frequency_penalty=None, max_generated_tokens=None,
        do_sample=None, seed=None, timeout_ms=600000):
        """
        Generate complete text response in non-streaming mode with vision input.

        This method generates the complete response and returns it as a single string.
        Use this when you need the full response before proceeding, or for batch processing
        where streaming is not required.

        Args:
            prompt (str or list): Input prompt. Can be:
                - str: Raw text prompt, to be inserted to the model as is.
                - list: Structured chat format with vision content [{"role": "user", "content": [{"type": "text", "text": "..."}, {"type": "image"}]}]
            frames (list): List of numpy arrays representing input images/frames. Empty list means no vision input. Must match model's expected input format.
            temperature (float, optional): Sampling temperature (0.0-2.0).
                Lower values = more deterministic, higher = more creative.
            top_p (float, optional): Nucleus sampling threshold (0.0-1.0).
                Only consider tokens with cumulative probability up to this value.
            top_k (int, optional): Top-k sampling limit. Only consider top-k most likely tokens.
            frequency_penalty (float, optional): Repetition penalty. Positive values
                discourage repetition.
            max_generated_tokens (int, optional): Maximum number of tokens to generate.
                If None, uses model default.
            do_sample (bool, optional): Whether to use sampling (True) or greedy decoding (False).
            seed (int, optional): Random seed for reproducible outputs.
                If None, uses random seed.
            timeout_ms (int, optional): Timeout in milliseconds.

        Returns:
            str: Complete generated response as a single string.

        Example::

            # Chat-style interaction with image
            prompt = [
                {"role": "user", "content": [
                    {"type": "text", "text": "What objects do you see?"},
                    {"type": "image"}
                ]}
            ]
            print(vlm.generate_all(prompt, frames=[image_array], temperature=0.1))

            # Simple text generation (without vision)
            response = vlm.generate_all("Tel me a joke", frames=[])

        Note:
            The conversation context is automatically maintained between calls.
            Use ``clear_context()`` to reset the conversation history.
        """
        with self.generate(prompt, frames, temperature, top_p, top_k, frequency_penalty,
                           max_generated_tokens, do_sample, seed) as gen_completion:
            return gen_completion.read_all(timeout_ms)

    def tokenize(self, text):
        """
        Tokenize input text using the model's tokenizer.

        This method converts text into a list of token IDs that the model uses internally.
        Useful for understanding how the model processes input, for debugging and
        for understanding how many tokens are needed for a given prompt.

        Args:
            text (str): Text to tokenize.

        Returns:
            list: List of token IDs (integers) representing the input text.

        Example::

            tokens = vlm.tokenize("Describe this image")
            print(f"Token IDs: {tokens}")
            print(f"Number of tokens: {len(tokens)}")
        """
        with ExceptionWrapper():
            return self._vlm.tokenize(text)

    def get_context_usage_size(self):
        """
        Get the context usage size of the VLM model.

        This method returns the total number of tokens currently stored in the model's context,
        including both input tokens and generated tokens from previous interactions.
        The context usage includes the conversation history and any tokens that have been
        processed but not yet cleared by clear_context().

        Returns:
            int: Current number of tokens in the context.
        """
        with ExceptionWrapper():
            return self._vlm.get_context_usage_size()

    def max_context_capacity(self):
        """
        Obtain the maximum context capacity of the VLM model.

        This method returns the maximum number of tokens that can be stored in the model's context.
        This value is determined in the model compilation,
        represents the upper limit for context storage, and is not configurable.

        Returns:
            int: Maximum number of tokens that can be stored in the context.

        Example::

            max_capacity = vlm.max_context_capacity()
            current_usage = vlm.get_context_usage_size()
            print(f"Context usage: {current_usage}/{max_capacity}")
        """
        with ExceptionWrapper():
            return self._vlm.max_context_capacity()

    def clear_context(self):
        """
        Clear the conversation context/history.

        This method resets the model's memory of previous interactions, starting
        fresh for the next generation. Call this between separate conversations
        or when you want to start a new topic without prior context.

        Example::

            # First conversation
            response1 = vlm.generate_all("What's in this image?", frames=[image1])
            response2 = vlm.generate_all("What about the colors?", frames=[])  # Will remember image1

            # Clear context and start fresh
            vlm.clear_context()
            response3 = vlm.generate_all("What about the colors?", frames=[])  # Won't remember image1

        Note:
            This operation is irreversible. Once context is cleared, previous
            conversation history cannot be recovered.
        """
        with ExceptionWrapper():
            self._vlm.clear_context()

    def get_generation_recovery_sequence(self):
        """
        Get the current generation recovery sequence.

        Recovery sequences are used to handle error conditions during generation,
        or for when a generation is interrupted (e.g. by reaching max_generated_tokens).
        This is an advanced feature for error handling and debugging.

        Returns:
            str: Recovery sequence string, to be inserted to the model when a generation is interrupted.

        Note:
            This is an advanced feature. Most users won't need to use this method.
        """
        with ExceptionWrapper():
            return self._vlm.get_generation_recovery_sequence()

    def set_generation_recovery_sequence(self, sequence):
        """
        Set a custom generation recovery sequence.

        Recovery sequences are used to handle error conditions during generation,
        or for when a generation is interrupted (e.g. by reaching max_generated_tokens).
        This is an advanced feature for error handling and debugging.

        Args:
            sequence (str): Recovery sequence string, to be inserted to the model when a generation is interrupted.

        Note:
            This is an advanced feature. Most users won't need to use this method.
        """
        with ExceptionWrapper():
            self._vlm.set_generation_recovery_sequence(sequence)

    def prompt_template(self):
        """
        Get the model's prompt template format.

        Different models use different prompt templates (chat template) for translating
        structured prompts (list of dicts) to raw text prompts.
        This method returns information about the expected format for this model.

        Returns:
            str: Prompt template as a string (Jinja2 template).

        Example::

            template = vlm.prompt_template()
            print(f"Model expects format: {template}")

        Note:
            When using structured prompts (list of dicts), the template is applied
            automatically. This method is useful when constructing raw text prompts.
        """
        with ExceptionWrapper():
            return self._vlm.prompt_template()

    def set_stop_tokens(self, stop_tokens):
        """
        Set custom stop tokens for generation.

        Stop tokens are sequences that, when generated, will cause the model
        to stop generating additional text. This is useful for controlling
        response length and format.

        Args:
            stop_tokens (list): List of strings that when generated will stop the generation.
        """
        with ExceptionWrapper():
            self._vlm.set_stop_tokens(stop_tokens)

    def get_stop_tokens(self):
        """
        Get the currently configured stop tokens.

        Returns:
            list: List of strings, where each string is a current stop token.

        Example::

            current_stops = vlm.get_stop_tokens()
            print(f"Current stop tokens: {current_stops}")
        """
        with ExceptionWrapper():
            return self._vlm.get_stop_tokens()

    def save_context(self):
        """
        Save the current context of the VLM as a binary blob.

        This method saves the current conversation context (including all previous
        interactions) as a binary blob that can be used to restore the context later
        using load_context(). This is useful for saving conversation state between
        sessions or for implementing context switching.

        Returns:
            bytes: Binary blob containing the saved context data.

        Example::

            # Save context after a conversation
            context_data = vlm.save_context()

            # Save to file for later use
            with open("conversation_context.bin", "wb") as f:
                f.write(context_data)

        Note:
            - The context is unique for a specific VLM model
            - Trying to load a context from a different model will result in an error
            - The context includes all conversation history up to this point
        """
        with ExceptionWrapper():
            return bytes(self._vlm.save_context())

    def load_context(self, context_data):
        """
        Load a previously saved context into the VLM.

        This method restores a conversation context that was previously saved using
        save_context(). This allows to continue the conversation from where it
        left off, or to switch between different conversation contexts.

        Args:
            context_data (bytes): Binary blob containing the saved context data.

        Example::

            # Load context from file
            with open("conversation_context.bin", "rb") as f:
                context_data = f.read()

            # Restore the conversation context
            vlm.load_context(context_data)

            # Continue the conversation
            response = vlm.generate_all("What were we talking about?", frames=[])

        Note:
            - The context must be from the same VLM model
            - Loading a context will replace the current context completely
            - This operation will fail if the context is from a different model
        """
        with ExceptionWrapper():
            self._vlm.load_context(list(context_data))

    def input_frame_size(self):
        """
        Get the expected input frame size in bytes.

        This method returns the total size in bytes that each input frame should have.
        Use this to ensure your numpy arrays have the correct total byte size.

        Returns:
            int: Expected frame size in bytes.

        Example::

            frame_size = vlm.input_frame_size()
            print(f"Expected frame size: {frame_size} bytes")
        """
        with ExceptionWrapper():
            return self._vlm.input_frame_size()

    def input_frame_shape(self):
        """
        Get the expected input frame shape.

        This method returns the expected dimensions for input frames.
        Use this to ensure your numpy arrays have the correct shape.

        Returns:
            list: Frame shape as (height, width, channels).

        Example::

            shape = vlm.input_frame_shape()
            print(f"Expected frame shape: {shape}")
            # Create a frame with correct shape
            frame = np.zeros(shape, dtype=np.uint8)
        """
        with ExceptionWrapper():
            return self._vlm.input_frame_shape()

    def input_frame_format_type(self):
        """
        Get the expected input frame data type.

        This method returns the expected numpy data type for input frames.
        Use this to ensure your numpy arrays have the correct dtype.

        Returns:
            numpy.dtype: Expected data type for input frames.

        Example::

            dtype = vlm.input_frame_format_type()
            print(f"Expected frame dtype: {dtype}")
            # Create a frame with correct dtype
            frame = np.zeros((224, 224, 3), dtype=dtype)
        """
        with ExceptionWrapper():
            return self._vlm.input_frame_format_type()

    def input_frame_format_order(self):
        """
        Get the expected input frame format order.

        This method returns the expected channel ordering for input frames
        (Usually NHWC, which means (height, width, channels)).

        Returns:
            _pyhailort.hailo_format_order_t: Expected format order for input frames.

        Example::

            format_order = vlm.input_frame_format_order()
            print(f"Expected format order: {format_order}")
        """
        with ExceptionWrapper():
            return self._vlm.input_frame_format_order()


class SegmentInfo:
    """
    Represents a transcribed audio segment with timing information.

    This class contains the transcribed text along with start and end timestamps
    for a specific segment of the audio input.

    Example::

        for segment in segments:
            print(f"[{segment.start_sec:.2f}s-{segment.end_sec:.2f}s] {segment.text}")
    """

    def __init__(self, segment_info):
        """
        Initialize a SegmentInfo object.

        Args:
            segment_info: Internal segment object containing timing and text data.
        """
        self._segment_info = segment_info

    @property
    def start_sec(self):
        """
        Get the start time of the segment.

        Returns:
            float: Start time in seconds, relative to the start of the audio.
        """
        return self._segment_info.start_sec

    @property
    def end_sec(self):
        """
        Get the end time of the segment.

        Returns:
            float: End time in seconds, relative to the start of the audio.
        """
        return self._segment_info.end_sec

    @property
    def text(self):
        """
        Get the transcribed text of the segment.

        Returns:
            str: Transcribed text for this segment.
        """
        return self._segment_info.text

    def __repr__(self):
        return f"SegmentInfo(start_sec={self.start_sec}, end_sec={self.end_sec}, text='{self.text}')"


class Speech2TextTask(Enum):
    """
    The available Speech2Text task types.

    This enum defines the supported task types for speech processing:

    - TRANSCRIBE: Transcribe speech to text in the same language
    - TRANSLATE: Translate speech to text in a different language
    """
    TRANSCRIBE = _pyhailort.Speech2TextTask.TRANSCRIBE
    TRANSLATE = _pyhailort.Speech2TextTask.TRANSLATE


class Speech2Text:
    """
    Speech-to-Text model inference interface for Hailo devices.

    This class provides a high-level Python interface for running Speech-to-Text models
    on Hailo hardware. It supports audio transcription and translation with configurable
    language settings and task types.

    Features:
        - Audio transcription with timestamped segments
        - Language translation capabilities
        - Support for multiple output formats (segments vs complete text)
        - Configurable task types (transcribe/translate)
        - Language-specific processing

    Example::

        # Basic transcription
        with VDevice() as vd:
            with Speech2Text(vd, "whisper.hef") as s2t:
                print(s2t.generate_all_text(task=Speech2TextTask.TRANSCRIBE, language="en", audio_data=audio_array))

        # Transcription with segments
        with VDevice() as vd:
            with Speech2Text(vd, "whisper.hef") as s2t:
                all_segments = s2t.generate_all_segments(task=Speech2TextTask.TRANSCRIBE, language="en", audio_data=audio_array)
                print(segment for segment in all_segments)

    Note:
        - The VDevice must remain active for the lifetime of the Speech2Text instance.
        - Using this class within a context manager (``with`` statement) is recommended to ensure proper resource cleanup.
        - Audio input must be in PCM float32 format (normalized to [-1.0, 1.0), mono, little-endian, 16 kHz).
    """

    def __init__(self, vdevice, model_path):
        """
        Initialize the Speech2Text model with a Hailo device and model file.

        Args:
            vdevice (VDevice): An active VDevice instance managing Hailo hardware.
            model_path (str): Path to the compiled HEF (Hailo Executable Format) file.

        Example::

            with VDevice() as vd:
                s2t = Speech2Text(vd, "models/whisper.hef")
        """
        self._vdevice = vdevice
        self._speech2text = _pyhailort.Speech2Text.create(vdevice._vdevice, model_path)

    def release(self):
        """
        Release internal resources and cleanup.

        This method is called automatically when exiting the context manager.
        Manual calls are needed only in case context manager is not used.
        """
        with ExceptionWrapper():
            self._speech2text.release()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()
        return False

    def generate_all_text(self, audio_data, task=Speech2TextTask.TRANSCRIBE, language="en", timeout_ms=10000):
        """
        Generate complete transcription as a single string.

        Args:
            audio_data (numpy.ndarray): Audio data as numpy array in PCM float32 format (normalized to [-1.0, 1.0), mono, little-endian, 16 kHz).
            task (Speech2TextTask, optional): Task to perform (transcribe or translate). Defaults to TRANSCRIBE.
            language (str, optional): Language to use for translation, in the format of ISO-639-1 two-letter code, for example: "en", "fr", etc. Defaults to "en".
            timeout_ms (int, optional): Timeout in milliseconds. Defaults to 10000.

        Returns:
            str: Complete transcription as a single string.

        Example::

            complete_text = s2t.generate_all_text(task=Speech2TextTask.TRANSCRIBE, language="en", audio_data=audio_array)
            print(f"Transcription: {complete_text}")
        """
        with ExceptionWrapper():
            return self._speech2text.generate_all_text(audio_data, task.value, language, timeout_ms)

    def generate_all_segments(self, audio_data, task=Speech2TextTask.TRANSCRIBE, language="en", timeout_ms=10000):
        """
        Generate transcription with timestamped segments.

        Args:
            audio_data (numpy.ndarray): Audio data as numpy array in PCM float32 format (normalized to [-1.0, 1.0), mono, little-endian, 16 kHz).
            task (Speech2TextTask, optional): Task to perform (transcribe or translate). Defaults to TRANSCRIBE.
            language (str, optional): Language to use for translation, in the format of ISO-639-1 two-letter code, for example: "en", "fr", etc. Defaults to "en".
            timeout_ms (int, optional): Timeout in milliseconds. Defaults to 10000.

        Returns:
            list[SegmentInfo]: List of SegmentInfo objects containing start/end timestamps and transcribed text.

        Example::

            segments = s2t.generate_all_segments(task=Speech2TextTask.TRANSCRIBE, language="en", audio_data=audio_array)
            for segment in segments:
                print(f"[{segment.start_sec:.2f}s-{segment.end_sec:.2f}s] {segment.text}")
        """
        with ExceptionWrapper():
            raw_segments = self._speech2text.generate_all_segments(audio_data, task.value, language, timeout_ms)
            return [SegmentInfo(segment) for segment in raw_segments]

    def tokenize(self, text):
        """
        Tokenize input text using the model's tokenizer.

        This method converts text into a list of token IDs that the model uses internally.
        Useful for understanding how the model processes input, for debugging and
        for understanding what is the tokens count of specific text.

        Args:
            text (str): Text to tokenize.

        Returns:
            list: List of token IDs (integers) representing the input text.

        Example::

            tokens = s2t.tokenize("Hello, world!")
            print(f"Token IDs: {tokens}")
            print(f"Number of tokens: {len(tokens)}")
        """
        with ExceptionWrapper():
            return self._speech2text.tokenize(text)