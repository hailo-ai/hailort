#!/usr/bin/env python

"""Hailo hardware API"""
from __future__ import division

import gc
import os

from contextlib import contextmanager

from hailo_platform.pyhailort.control_object import UdpHcpControl, PcieHcpControl
from hailo_platform.common.logger.logger import default_logger

from hailo_platform.pyhailort.pyhailort import (InternalEthernetDevice, InternalPcieDevice,
                                                HailoRTTransformUtils, HailoUdpScan, HailoRTException, BoardInformation)


class InferenceTargets(object):
    """Enum-like class with all inference targets supported by the HailoRT."""
    UNINITIALIZED = 'uninitialized'
    UDP_CONTROLLER = 'udp'
    PCIE_CONTROLLER = 'pcie'

class HailoHWObjectException(Exception):
    """Raised in any error related to Hailo hardware."""
    pass


class HailoHWObject(object):
    """Abstract Hailo hardware device representation (deprecated)"""

    NAME = InferenceTargets.UNINITIALIZED
    IS_HARDWARE = True

    def __init__(self):
        """Create the Hailo hardware object."""
        self._last_interact_time = None
        self._total_time = None
        self._id = None
        self._hw_arch = None
        self._logger = default_logger()
        self._debug = False
        self._is_device_used = False
        self._hef_loaded = False

        self._logger.warning("HailoHWObject is deprecated! Please use VDevice/Device object.")

    # TODO: HRT-6310 Remove this.
    def __eq__(self, other):
        return type(self).NAME == other

    @property
    def name(self):
        """str: The name of this target. Valid values are defined by :class:`~hailo_platform.pyhailort.hw_object.InferenceTargets` (deprecated)"""
        self._logger.warning("HailoHWObject name property is deprecated! Please use VDevice/Device object with device_id.")
        return type(self).NAME

    @property
    def is_hardware(self):
        """bool: Indicates this target runs on a physical hardware device. (deprecated)"""
        # TODO: SDK should implement in Target
        self._logger.warning("HailoHWObject is_hardware property is deprecated! Please use VDevice/Device object, or derive from it.")
        return type(self).IS_HARDWARE

    @property
    def device_id(self):
        """Getter for the device_id.
        Returns:
            str: A string ID of the device. BDF for PCIe devices, IP address for Ethernet devices, "Core" for core devices.
        """
        return self._id

    @property
    def sorted_output_layer_names(self):
        """Getter for the property sorted_output_names (deprecated).
        Returns:
            list of str: Sorted list of the output layer names.
        """
        self._logger.warning("HailoHWObject sorted_output_layer_names property is deprecated! Please use ConfiguredNetwork get_sorted_output_names.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to sorted_output_layer_names is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_sorted_output_names()

    @contextmanager
    def use_device(self, *args, **kwargs):
        """A context manager that wraps the usage of the device. (deprecated)"""
        self._logger.warning("HailoHWObject use_device context manager is deprecated! Please use VDevice/Device object.")
        self._is_device_used = True
        yield
        self._is_device_used = False

    def get_output_device_layer_to_original_layer_map(self):
        """Get a mapping between the device outputs to the layers' names they represent (deprecated).

        Returns:
            dict: Keys are device output names and values are lists of layers' names.
        """
        self._logger.warning("HailoHWObject get_output_device_layer_to_original_layer_map function is deprecated!")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to layer names is only allowed when there is a single loaded network group")
        return {stream_info.name : self._loaded_network_groups[0].get_vstream_names_from_stream_name(stream_info.name)
            for stream_info in self.get_output_stream_infos()}

    def get_original_layer_to_device_layer_map(self):
        """Get a mapping between the layer names and the device outputs that contain them (deprecated).

        Returns:
            dict: Keys are the names of the layers and values are device outputs names.
        """
        self._logger.warning("HailoHWObject get_original_layer_to_device_layer_map function is deprecated!")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to layer names is only allowed when there is a single loaded network group")
        return {vstream_info.name : self._loaded_network_groups[0].get_stream_names_from_vstream_name(vstream_info.name)
            for vstream_info in self.get_output_vstream_infos()}

    @property
    def device_input_layers(self):
        """Get a list of the names of the device's inputs. (deprecated)"""
        self._logger.warning("HailoHWObject device_input_layers function is deprecated! Please use ConfiguredNetwork object.")
        return [layer.name for layer in self.get_input_stream_infos()]

    @property
    def device_output_layers(self):
        """Get a list of the names of the device's outputs. (deprecated)"""
        self._logger.warning("HailoHWObject device_output_layers function is deprecated! Please use ConfiguredNetwork object.")
        return [layer.name for layer in self.get_output_stream_infos()]

    def hef_loaded(self):
        """Return True if this object has loaded the model HEF to the hardware device. (deprecated)"""
        # TODO: SDK should implement in Target
        self._logger.warning("HailoHWObject hef_loaded function is deprecated! Please use VDevice/Device object, or derive from it.")
        return self._hef_loaded

    def outputs_count(self):
        """Return the amount of output tensors that are returned from the hardware device for every
        input image (deprecated).
        """
        self._logger.warning("HailoHWObject outputs_count function is deprecated! Please use ConfiguredNetwork object.")
        return len(self.get_output_vstream_infos())

    def _clear_shapes(self):
        # TODO: SDK should implement in Target
        self._logger.warning("HailoHWObject _clear_shapes function is deprecated! Please use ConfiguredNetwork object.")

    @property
    def model_name(self):
        """Get the name of the current model (deprecated).

        Returns:
            str: Model name.
        """
        self._logger.warning("HailoHWObject model_name property is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) == 1:
            return self._loaded_network_groups[0].name
        raise HailoHWObjectException(
            "This function is only supported when there is exactly 1 loaded network group. Use HEF.get_network_group_names() / ConfiguredNetwork.name / ActivatedNetwork.name")

    def get_output_shapes(self):
        """Get the model output shapes, as returned to the user (without any hardware padding) (deprecated).

        Returns:
            Tuple of output shapes, sorted by the output names.
        """
        self._logger.warning("HailoHWObject get_output_shapes function is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Calling get_output_shapes is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_output_shapes()


class HailoChipObject(HailoHWObject):
    """Hailo hardware device representation (deprecated)"""

    def __init__(self):
        """Create the Hailo Chip hardware object."""
        super(HailoChipObject, self).__init__()
        self._id = "Generic Hailo Device"
        self._control_object = None
        self._loaded_network_groups = []
        self._creation_pid = os.getpid()

    @property
    def control(self):
        """:class:`HailoControl <hailo_platform.pyhailort.control_object.HailoControl>`: Returns
        the control object of this device, which implements the control API of the Hailo device.
        .. attention:: Use the low level control API with care.
        """
        if self._control_object is None:
            raise HailoRTException(
                "The device has been released and is not usable."
                " Device is released when the function `release()` is called explicitly, or when created using a context manager and goes out of scope.")
        return self._control_object

    def get_all_input_layers_dtype(self):
        """Get the model inputs dtype (deprecated).

        Returns:
            dict of :obj:'numpy.dtype': where the key is model input_layer name, and the value is dtype as the device expect to get for this input. 
        """
        self._logger.warning("HailoChipObject get_all_input_layers_dtype function is deprecated! Please use ConfiguredNetwork object.")
        return {stream.name: HailoRTTransformUtils.get_dtype(stream.data_bytes) for stream in self.get_input_stream_infos()}

    def get_input_vstream_infos(self, network_name=None):
        """Get input vstreams information of a specific network group (deprecated).

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            If there is exactly one configured network group, returns a list of
            :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input vstreams
        """
        self._logger.warning("HailoChipObject get_input_vstream_infos function is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to network vstream info is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_input_vstream_infos(network_name=network_name)

    def get_output_vstream_infos(self, network_name=None):
        """Get output vstreams information of a specific network group (deprecated).

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            If there is exactly one configured network group, returns a list of
            :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all output vstreams
        """
        self._logger.warning("HailoChipObject get_output_vstream_infos function is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to network vstream info is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_output_vstream_infos(network_name=network_name)

    def get_all_vstream_infos(self, network_name=None):
        """Get input and output vstreams information (deprecated).

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            If there is exactly one configured network group, returns a list of
            :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input and output vstreams
        """
        self._logger.warning("HailoChipObject get_all_vstream_infos function is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to network vstream info is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_all_vstream_infos(network_name=network_name)

    def get_input_stream_infos(self, network_name=None):
        """Get the input low-level streams information of a specific network group (deprecated).

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            If there is exactly one configured network group, returns a list of
            :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with information objects
            of all input low-level streams.
        """
        self._logger.warning("HailoChipObject get_input_stream_infos function is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to network stream info is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_input_stream_infos(network_name=network_name)

    def get_output_stream_infos(self, network_name=None):
        """Get the output low-level streams information of a specific network group (deprecated).

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            If there is exactly one configured network group, returns a list of
            :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with information objects
            of all output low-level streams.
        """
        self._logger.warning("HailoChipObject get_output_stream_infos function is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to network stream info is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_output_stream_infos(network_name=network_name)

    def get_all_stream_infos(self, network_name=None):
        """Get input and output streams information of a specific network group (deprecated).

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            If there is exactly one configured network group, returns a list of
            :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with all the information objects of all input and output streams
        """
        self._logger.warning("HailoChipObject get_all_stream_infos function is deprecated! Please use ConfiguredNetwork object.")
        if len(self._loaded_network_groups) != 1:
            raise HailoHWObjectException("Access to network stream info is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0].get_all_stream_infos(network_name=network_name)

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
            raise HailoHWObjectException("Access to network layer info is only allowed when there is a single loaded network group")
        return self._loaded_network_groups[0]

    def get_input_shape(self, name=None):
        """Get the input shape (not padded) of a network (deprecated).

        Args:
            name (str, optional): The name of the desired input. If a name is not provided, return
                the first input_dataflow shape.

        Returns:
            Tuple of integers representing the input_shape.
        """
        self._logger.warning("HailoChipObject get_input_shape function is deprecated! Please use ConfiguredNetwork object.")
        if name is None:
            name = self.get_input_vstream_infos()[0].name

        for input_vstream in self.get_input_vstream_infos():
            if input_vstream.name == name:
                return input_vstream.shape

        raise HailoHWObjectException("There is no input named {}! the input names are: {}".format(name,
            [input_vstream.name for input_vstream in self.get_input_vstream_infos()]))

    def get_index_from_name(self, name):
        """Get the index in the output list from the name (deprecated).

        Args:
            name (str): The name of the output.

        Returns:
            int: The index of the layer name in the output list.
        """
        self._logger.warning("HailoChipObject get_index_from_name function is deprecated! Please use ConfiguredNetwork object.")
        try:
            return self.sorted_output_layer_names.index(name)
        except ValueError:
            if len(self.sorted_output_layer_names) == 1:
                # Case regard to SDK-9366 - see Jira for details.
                self._logger.warning('Incorrect meta item - layer defuse_name does not match layer name.')
                return 0
            else:
                raise HailoHWObjectException("Could not get index for outputs properly.")

    def release(self):
        """
            Release the allocated resources of the device. This function should be called when working with the device not as context-manager.
            Note: After calling this function, the device will not be usable.
        """
        if self._device is not None:
            self._device.release()
            self._device = None
            self._control_object = None


class EthernetDevice(HailoChipObject):
    """Represents any Hailo hardware device that supports UDP control and dataflow (deprecated)"""

    NAME = InferenceTargets.UDP_CONTROLLER

    def __init__(
            self,
            remote_ip,
            remote_control_port=22401):
        """Create the Hailo UDP hardware object.

        Args:
            remote_ip (str): Device IP address.
            remote_control_port (int, optional): UDP port to which the device listens for control.
                Defaults to 22401.
        """

        super(EthernetDevice, self).__init__()

        self._logger.warning("EthernetDevice is deprecated! Please use VDevice/Device object.")

        gc.collect()

        self._remote_ip = remote_ip
        self._remote_control_port = remote_control_port
        # EthernetDevice __del__ function tries to release self._device.
        # to avoid AttributeError if the __init__ func fails, we set it to None first.
        # https://stackoverflow.com/questions/6409644/is-del-called-on-an-object-that-doesnt-complete-init
        self._device = None
        self._control_object = None

        self._open_device()
        self._id = "{}".format(self._remote_ip)
        identity = self._control_object._identify_info
        self._hw_arch = BoardInformation.get_hw_arch_str(identity.device_architecture)

    @staticmethod
    def scan_devices(interface_name, timeout_seconds=3):
        """Scans for all eth devices on a specific network interface.

        Args:
            interface_name (str): Interface to scan.
            timeout_seconds (int, optional): timeout for scan operation. Defaults to 3.
        Returns:
            list of str: IPs of scanned devices.
        """
        default_logger().warning("EthernetDevice scan_devices method is deprecated! Please use scan() of Device object.")
        udp_scanner = HailoUdpScan()
        return udp_scanner.scan_devices(interface_name, timeout_seconds=timeout_seconds)

    def _open_device(self):
        self._device = InternalEthernetDevice(self._remote_ip, self._remote_control_port)
        self._control_object = UdpHcpControl(self._remote_ip, device=self._device, remote_control_port=self._remote_control_port)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()
        return False

    def __del__(self):
        self.release()

    @property
    def remote_ip(self):
        """Return the IP of the remote device (deprecated)."""
        self._logger.warning("EthernetDevice remote_ip method is deprecated! Please use VDevice/Device object.")
        return self._remote_ip


class PcieDevice(HailoChipObject):
    """Hailo PCIe production device representation (deprecated)"""

    NAME = InferenceTargets.PCIE_CONTROLLER

    def __init__(
            self,
            device_info=None):

        """Create the Hailo PCIe hardware object.

        Args:
            device_info (:obj:`hailo_platform.pyhailort.pyhailort.PcieDeviceInfo`, optional): Device info to create, call
                :func:`PcieDevice.scan_devices` to get list of all available devices.
        """
        super(PcieDevice, self).__init__()
        self._logger.warning("PcieDevice is deprecated! Please use VDevice/Device object.")

        gc.collect()
        # PcieDevice __del__ function tries to release self._device.
        # to avoid AttributeError if the __init__ func fails, we set it to None first.
        # https://stackoverflow.com/questions/6409644/is-del-called-on-an-object-that-doesnt-complete-init
        self._device = None
        self._device_info = None
        self._control_object = None

        self._open_device(device_info)

        # At this point self._device_info is already initialized
        self._id = "{}".format(self._device_info)
        identity = self._control_object._identify_info
        self._hw_arch = BoardInformation.get_hw_arch_str(identity.device_architecture)

    @staticmethod
    def scan_devices():
        """Scans for all pcie devices on the system (deprecated).

        Returns:
            list of :obj:`hailo_platform.pyhailort.pyhailort.PcieDeviceInfo`
        """
        default_logger().warning("PcieDevice scan_devices method is deprecated! Please use Device object.")
        return InternalPcieDevice.scan_devices()

    def _open_device(self, device_info):
        self._device = InternalPcieDevice(device_info)
        self._device_info = self._device._device_info # Handeling a case where device_info is None
        self._control_object = PcieHcpControl(device=self._device, device_info=self._device_info)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()
        return False

    def __del__(self):
        self.release()
