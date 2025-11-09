#!/usr/bin/env python
import os
import sys
import pathlib
import pprint

class MissingPyHRTLib(Exception):
    pass


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

def _verify_pyhailort_lib_exists():
    python_version = "".join(str(i) for i in sys.version_info[:2])
    lib_extension = {
        "posix": "so",
        "nt": "pyd",  # Windows
    }[os.name]

    path = f"{__path__[0]}/pyhailort/"
    if next(pathlib.Path(path).glob(f"_pyhailort*.{lib_extension}"), None) is None:
        raise MissingPyHRTLib(f"{path} should include a _pyhailort library (_pyhailort*{python_version}*.{lib_extension}). Includes: {pprint.pformat(list(pathlib.Path(path).iterdir()))}")
_verify_pyhailort_lib_exists()

__version__ = "5.1.1"
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
