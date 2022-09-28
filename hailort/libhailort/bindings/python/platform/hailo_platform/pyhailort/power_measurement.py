from hailo_platform.pyhailort.pyhailort import (DvmTypes, PowerMeasurementTypes,  # noqa F401
                                                SamplingPeriod, AveragingFactor,
                                                HailoPowerMeasurementUtils, MeasurementBufferIndex, HailoRTException)
import warnings
warnings.warn('Importing power_measurements directly is deprecated. One should use direct hailo_platform instead')
