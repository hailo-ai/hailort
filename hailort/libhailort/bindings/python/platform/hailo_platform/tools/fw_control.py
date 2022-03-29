#!/usr/bin/env python
from hailo_platform.common.tools.cmd_utils.base_utils import HailortCliUtil


class ScanCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'scan')


class ControlCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'fw-control')

class LoggerCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'fw-logger')

class MeasurePowerCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'measure-power')