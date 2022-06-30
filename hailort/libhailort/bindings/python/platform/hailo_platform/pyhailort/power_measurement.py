from hailo_platform.pyhailort.pyhailort import (DvmTypes, PowerMeasurementTypes,  # noqa F401
                                                      SamplingPeriod, AveragingFactor,
                                                      HailoPowerMeasurementUtils, MeasurementBufferIndex, HailoRTException)

# https://github.com/pybind/pybind11/issues/253
import re
def enum_to_dict(enum):
    return {k: v for k, v in enum.__dict__.items() if not re.match("__(.*)", str(k)) and isinstance(v, enum)}

def _get_buffer_index_enum_member(index):
    for name, member in enum_to_dict(MeasurementBufferIndex).items():
        if int(member) == index:
            return member
    raise HailoRTException("Invalid index")
