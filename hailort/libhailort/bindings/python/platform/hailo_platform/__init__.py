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


from hailo_platform.tools.udp_rate_limiter import UDPRateLimiter
from hailo_platform.pyhailort.hw_object import PcieDevice, EthernetDevice
from hailo_platform.pyhailort.pyhailort import (HEF, ConfigureParams,
                                                FormatType, FormatOrder,
                                                MipiDataTypeRx, MipiPixelsPerClock,
                                                MipiClockSelection, MipiIspImageInOrder,
                                                MipiIspImageOutDataType, IspLightFrequency, HailoPowerMode,
                                                Endianness, HailoStreamInterface,
                                                InputVStreamParams, OutputVStreamParams,
                                                InputVStreams, OutputVStreams,
                                                InferVStreams, HailoStreamDirection, HailoFormatFlags, HailoCpuId, Device, VDevice,
                                                DvmTypes, PowerMeasurementTypes, SamplingPeriod, AveragingFactor, MeasurementBufferIndex,
                                                HailoRTException, YOLOv5PostProcessOp, HailoSchedulingAlgorithm)

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

def get_version(package_name):
    # See: https://packaging.python.org/guides/single-sourcing-package-version/ (Option 5)
    # We assume that the installed package is actually the same one we import. This assumption may
    # break in some edge cases e.g. if the user modifies sys.path manually.
    
    # hailo_platform package has been renamed to hailort, but the import is still hailo_platform
    if package_name == "hailo_platform":
        package_name = "hailort"
    try:
        import pkg_resources
        return pkg_resources.get_distribution(package_name).version
    except:
        return 'unknown'

__version__ = get_version('hailo_platform')
__all__ = ['EthernetDevice', 'DvmTypes', 'PowerMeasurementTypes',
           'SamplingPeriod', 'AveragingFactor', 'MeasurementBufferIndex', 'UDPRateLimiter', 'PcieDevice', 'HEF',
           'ConfigureParams', 'FormatType', 'FormatOrder', 'MipiDataTypeRx', 'MipiPixelsPerClock', 'MipiClockSelection',
           'MipiIspImageInOrder', 'MipiIspImageOutDataType', 'join_drivers_path', 'IspLightFrequency', 'HailoPowerMode',
           'Endianness', 'HailoStreamInterface', 'InputVStreamParams', 'OutputVStreamParams',
           'InputVStreams', 'OutputVStreams', 'InferVStreams', 'HailoStreamDirection', 'HailoFormatFlags', 'HailoCpuId',
           'Device', 'VDevice', 'HailoRTException', 'YOLOv5PostProcessOp', 'HailoSchedulingAlgorithm']
