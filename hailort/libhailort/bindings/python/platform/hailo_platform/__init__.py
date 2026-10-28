#!/usr/bin/env python
import os

# Must appear before other imports:
def join_drivers_path(path):
    _ROOT = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
    return os.path.join(_ROOT, 'hailo_platform', 'drivers', path)


def _register_hailort_dll_directories() -> None:
    """
    The MSI records the install root in the registry and we register its bin folder so Python finds libhailort.dll there
    without relying on PATH, as Python 3.8+ ignores PATH for an extension module's dependency DLLs.
    """
    import winreg
    hrt_key = r"SOFTWARE\Hailo\HailoRT"
    access = winreg.KEY_READ | winreg.KEY_WOW64_64KEY
    try:
        with winreg.OpenKey(key=winreg.HKEY_LOCAL_MACHINE, sub_key=hrt_key, reserved=0, access=access) as key:
            install_dir = winreg.QueryValueEx(key, "InstallDir")[0]
        bin_dir = os.path.join(install_dir, "bin")
        if os.path.isdir(bin_dir):
            os.add_dll_directory(bin_dir)
    except OSError:
        pass


if os.name == "nt":
    _register_hailort_dll_directories()


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
