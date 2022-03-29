#!/usr/bin/env python

from hailo_platform.common.tools.cmd_utils.base_utils import HailortCliUtil


class FWUpdaterCLI(HailortCliUtil):
    """Cli tool for firmware updates"""
    def __init__(self, parser):
        super().__init__(parser, 'fw-update')
