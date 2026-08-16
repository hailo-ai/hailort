#!/usr/bin/env python
import os

# Must appear before other imports:
def join_drivers_path(path):
    _ROOT = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
    return os.path.join(_ROOT, 'hailo_platform', 'drivers', path)


import hailo_platform.pyhailort._pyhailort as _pyhailort
from hailo_platform.pyhailort.pyhailort import (HEF, ConfigureParams,
                                                FormatType, FormatOrder,
                                                HailoPowerMode,
                                                Endianness, HailoStreamInterface,
                                                InputVStreamParams, OutputVStreamParams,
                                                InputVStreams, OutputVStreams,
                                                InferVStreams, HailoStreamDirection, HailoFormatFlags, HailoCpuId, Device, VDevice,
                                                DvmTypes, PowerMeasurementTypes, SamplingPeriod, AveragingFactor, MeasurementBufferIndex,
                                                HailoRTException, HailoSchedulingAlgorithm, HailoRTStreamAbortedByUser, AsyncInferJob,
                                                HailoCommunicationClosedException, HailoSessionListener, HailoSession)

from importlib.metadata import version as _pkg_version
__version__ = _pkg_version("hailort")
def _verify_version():
    if _pyhailort.__version__ != __version__:
        raise ImportError(
            f"_pyhailort version ({_pyhailort.__version__}) does not match pyhailort version ({__version__})"
        )
_verify_version()

__all__ = ['DvmTypes', 'PowerMeasurementTypes',
           'SamplingPeriod', 'AveragingFactor', 'MeasurementBufferIndex', 'HEF',
           'ConfigureParams', 'FormatType', 'FormatOrder', 'join_drivers_path', 'HailoPowerMode',
           'Endianness', 'HailoStreamInterface', 'InputVStreamParams', 'OutputVStreamParams',
           'InputVStreams', 'OutputVStreams', 'InferVStreams', 'HailoStreamDirection', 'HailoFormatFlags', 'HailoCpuId',
           'Device', 'VDevice', 'HailoRTException', 'HailoSchedulingAlgorithm', 'HailoRTStreamAbortedByUser', 'AsyncInferJob',
           'HailoCommunicationClosedException', 'HailoSessionListener', 'HailoSession']
