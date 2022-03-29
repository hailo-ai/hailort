from hailo_platform.drivers.hailort.pyhailort import (DvmTypes, PowerMeasurementTypes,  # noqa F401
                                                      SamplingPeriod, AveragingFactor,
                                                      HailoPowerMeasurementUtils)

""" Amount of time between each power measurement interval.
        The default values for provides by the sensor a new value every:
        2 * sampling_period (1.1) * averaging_factor (256) [ms].
        Therefore we want it to be the period of time that the core will sleep between samples,
        plus a factor of 20 percent  """
DEFAULT_POWER_MEASUREMENT_DELAY_PERIOD_MS = int((HailoPowerMeasurementUtils.return_real_sampling_period(SamplingPeriod.PERIOD_1100us) / 1000.0 *
                                                HailoPowerMeasurementUtils.return_real_averaging_factor(AveragingFactor.AVERAGE_256)  * 2) * 1.2)