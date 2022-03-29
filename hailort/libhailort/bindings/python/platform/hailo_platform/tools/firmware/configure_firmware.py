#!/usr/bin/env python

"""Parses a configuration file that contains user or board configurations.
"""
from hailo_platform.common.tools.cmd_utils.base_utils import HailortCliUtil

class FWConfigCommandCLI(HailortCliUtil):
    """CLI tool for changing the FW configuration (User Config)"""
    def __init__(self, parser):
        super().__init__(parser, 'fw-config')

class BoardConfigCommandCLI(HailortCliUtil):
    """CLI tool for changing the FW configuration (Board Config)"""
    def __init__(self, parser):
        super().__init__(parser, 'board-config')
