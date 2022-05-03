#!/usr/bin/env python
from __future__ import print_function
from builtins import object
import subprocess

from hailo_platform.common.logger.logger import default_logger


class CmdUtilsBaseUtilError(Exception):
    pass


class HailortCliUtilError(CmdUtilsBaseUtilError):
    pass


# Note: CmdUtilsBaseUtil and CmdUtilsBaseUtilError are external dependencies in phase2-sdk/demos repo; don't change!
class CmdUtilsBaseUtil(object):
    def __init__(self, args_parser):
        pass


class Helper(CmdUtilsBaseUtil):
    def __init__(self, commands):
        self._commands = commands

    def __call__(self, parser):
        parser.set_defaults(func=self.run)

    def run(self, args):
        for command_name, (help_, _) in self._commands.items():
            print("{} - {}".format(command_name, help_))


class HailortCliUtil(CmdUtilsBaseUtil):
    def __init__(self, args_parser, command):
        self._command = command
        self._logger = default_logger()

    @classmethod
    def _run_hailortcli(cls, binary_path, command, command_args):
        cmd_args = [binary_path, command] + command_args

        process = subprocess.Popen(cmd_args)
        try:
            _ = process.communicate()
            return process.returncode
        except KeyboardInterrupt:
            process.terminate()
            raise HailortCliUtilError('hailo has been interrupted by the user')

    def run(self, argv):
        hailortcli_cmd = 'hailortcli'
        self._logger.info('(hailo) Running command \'{}\' with \'hailortcli\''.format(self._command))
        return self._run_hailortcli(hailortcli_cmd, self._command, argv)
