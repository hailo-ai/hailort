#!/usr/bin/env python

from hailo_platform.common.tools.cmd_utils.base_utils import HailortCliUtil


class SSBUpdaterCLI(HailortCliUtil):
    """Cli tool for second stage boot updates"""
    def __init__(self, parser):
        super().__init__(parser, 'ssb-update')
