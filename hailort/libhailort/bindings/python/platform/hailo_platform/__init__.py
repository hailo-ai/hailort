#!/usr/bin/env python
import os
import sys
import pathlib
import pprint

from hailo_platform.common.paths_manager.version import get_version
from hailo_platform.common.paths_manager.paths import PackingInfo, PackingStatus

class MissingPyHRTLib(Exception):
    pass


# This hack checks which modules the user has on his computer.
# packing status set to "packed_client" if it can't import the PLATFORM_INTERNALS module
# it set the packing status to "standalone_platform" if it can't import the SDK_COMMON module

try:
    import hailo_platform_internals  # noqa F401
except ImportError:
    PackingInfo().status = PackingStatus.packed_client

try:
    import hailo_sdk_common # noqa F401
except:
    PackingInfo().status = PackingStatus.standalone_platform



# This hack only affects internals users with hailo_validation installed.
# It changes the packing status to PACKED if it thinks SDK was installed from
# wheel.
try:
    import hailo_validation  # noqa F401
    if 'site-packages/hailo_sdk' in __path__[0] :
        PackingInfo().status = PackingStatus.packed_client
except ImportError:
    pass


# Must appear before other imports:
def join_drivers_path(path):
    _ROOT = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
    return os.path.join(_ROOT, 'hailo_platform', 'drivers', path)


from hailo_platform.tools.udp_rate_limiter import UDPRateLimiter
from hailo_platform.drivers.hw_object import PcieDevice, EthernetDevice
from hailo_platform.drivers.hailo_controller.power_measurement import (DvmTypes,
                                                                       PowerMeasurementTypes,
                                                                       SamplingPeriod, AveragingFactor)
from hailo_platform.drivers.hailort.pyhailort import (HEF, ConfigureParams,
                                                      FormatType, FormatOrder,
                                                      MipiDataTypeRx, MipiPixelsPerClock,
                                                      MipiClockSelection, MipiIspImageInOrder,
                                                      MipiIspImageOutDataType, IspLightFrequency, HailoPowerMode,
                                                      Endianness, HailoStreamInterface,
                                                      InputVStreamParams, OutputVStreamParams,
                                                      InputVStreams, OutputVStreams,
                                                      InferVStreams, HailoStreamDirection, HailoFormatFlags, HailoCpuId, VDevice)

def _verify_pyhailort_lib_exists():
    python_version = "".join(str(i) for i in sys.version_info[:2])
    lib_extension = {
        "posix": "so",
        "nt": "pyd",  # Windows
    }[os.name]

    path = f"{__path__[0]}/drivers/hailort/"
    if next(pathlib.Path(path).glob(f"_pyhailort*.{lib_extension}"), None) is None:
        raise MissingPyHRTLib(f"{path} should include a _pyhailort library (_pyhailort*{python_version}*.{lib_extension}). Includes: {pprint.pformat(list(pathlib.Path(path).iterdir()))}")

_verify_pyhailort_lib_exists()


__version__ = get_version('hailo_platform')
__all__ = ['EthernetDevice', 'DvmTypes', 'PowerMeasurementTypes',
           'SamplingPeriod', 'AveragingFactor', 'UDPRateLimiter', 'PcieDevice', 'HEF',
           'ConfigureParams', 'FormatType', 'FormatOrder', 'MipiDataTypeRx', 'MipiPixelsPerClock', 'MipiClockSelection',
           'MipiIspImageInOrder', 'MipiIspImageOutDataType', 'join_drivers_path', 'IspLightFrequency', 'HailoPowerMode',
           'Endianness', 'HailoStreamInterface', 'InputVStreamParams', 'OutputVStreamParams',
           'InputVStreams', 'OutputVStreams', 'InferVStreams', 'HailoStreamDirection', 'HailoFormatFlags', 'HailoCpuId',
           'VDevice']
