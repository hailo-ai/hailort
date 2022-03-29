#!/usr/bin/env python
from builtins import object
import json

from contextlib2 import contextmanager


class InferenceTargetException(Exception):
    """Raised when an error related to the inference target has occurred."""
    pass


class InferenceTargets(object):
    """Enum-like class with all inference targets supported by the Hailo SDK.
    See the classes themselves for details about each target.
    """
    UNINITIALIZED = 'uninitialized'
    SDK_NATIVE = 'sdk_native'
    SDK_NATIVE_CLIPPED = 'sdk_native_clipped'
    SDK_NUMERIC = 'sdk_numeric'
    SDK_DEBUG_PRECISE_NUMERIC = 'sdk_debug_precise_numeric'
    SDK_PARTIAL_NUMERIC = 'sdk_partial_numeric'
    SDK_FINE_TUNE = 'sdk_fine_tune'
    SDK_MIXED = 'sdk_mixed'
    HW_SIMULATION = 'hw_sim'
    HW_SIMULATION_MULTI_CLUSTER = 'hw_sim_mc'
    FPGA = 'fpga'
    UDP_CONTROLLER = 'udp'
    PCIE_CONTROLLER = 'pcie'
    HW_DRY = 'hw_dry'
    HW_DRY_UPLOAD = 'hw_dry_upload'
    UV_WORKER = 'uv'
    DANNOX = 'dannox'


class InferenceObject(object):
    """Represents a target that can run models inference. The target can be either software based
    (eventually running on CPU/GPU), or Hailo hardware based.

    .. note:: This class should not be used directly. Use only its inherited classes.
    """
    NAME = InferenceTargets.UNINITIALIZED
    IS_NUMERIC = False
    IS_HARDWARE = False
    IS_SIMULATION = False

    def __new__(cls, *args, **kwargs):
        if cls.NAME == InferenceTargets.UNINITIALIZED:
            raise InferenceTargetException(
                '{} is an abstract target and cannot be used directly.'.format(cls.__name__))
        # object's __new__() takes no parameters
        return super(type(cls), cls).__new__(cls)

    def __init__(self):
        """Inference object constructor."""
        self._is_device_used = False

    def __eq__(self, other):
        return type(self).NAME == other

    # TODO: Required for Python2 BW compatibility (SDK-10038)
    # This impl' comes by default in Python3
    def __ne__(self, other):
        return not self.__eq__(other)

    @contextmanager
    def use_device(self, *args, **kwargs):
        """A context manager that should wrap any usage of the target."""
        self._is_device_used = True
        yield
        self._is_device_used = False

    @property
    def name(self):
        """str: The name of this target. Valid values are defined by
        :class:`InferenceObject <hailo_sdk_common.targets.inference_targets.InferenceTargets>`.
        """
        return type(self).NAME

    @property
    def is_numeric(self):
        """bool: Determines whether this target is working in numeric mode.
        """
        return type(self).IS_NUMERIC

    @property
    def is_hardware(self):
        """bool: Determines whether this target runs on a physical hardware device.
        """
        return type(self).IS_HARDWARE

    @property
    def is_simulation(self):
        """bool: Determines whether this target is used for HW simulation.
        """
        return type(self).IS_SIMULATION

    def _get_json_dict(self):
        json_dict = {'name': self.name,
                     'is_numeric': self.is_numeric,
                     'is_hardware': self.is_hardware,
                     'is_simulation': self.is_simulation}
        return json_dict

    def to_json(self):
        """Get a JSON representation of this object.

        Returns:
            str: A JSON dump.
        """
        return json.dumps(self._get_json_dict())
