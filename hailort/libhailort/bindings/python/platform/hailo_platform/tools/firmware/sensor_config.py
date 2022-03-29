#!/usr/bin/env python

"""Parses a configuration file that contains camera sensor configurations in the FW.
"""
from hailo_platform.common.tools.cmd_utils.base_utils import HailortCliUtil

class SensorConfigCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'sensor-config')