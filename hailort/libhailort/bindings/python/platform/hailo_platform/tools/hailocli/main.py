#!/usr/bin/env python

import argparse
import sys

import argcomplete

import hailo_platform
from hailo_platform.tools.hailocli.base_utils import HailortCliUtil, HailortCliUtilError, Helper
from hailo_platform.tools.hailocli.hailocli_commands import (BenchmarkCommandCLI, ControlCommandCLI, FWConfigCommandCLI,
                                                             FWUpdaterCLI, LoggerCommandCLI, MeasurePowerCommandCLI,
                                                             MonitorCommandCLI, ParseHEFCommandCLI, RunCommandCLI,
                                                             SSBUpdaterCLI, ScanCommandCLI, SensorConfigCommandCLI,
                                                             TutorialRunnerCLI, UDPRateLimiterCLI)
from hailo_platform.tools.hailocli.version_action import CustomVersionAction


# Note: PlatformCommands are external dependencies in phase2-sdk/demos repo; don't change!
class PlatformCommands:
    INVALID_COMMAND_EXIT_CODE = 1

    PLATFORM_COMMANDS = {
        'fw-update': ('Firmware update tool', FWUpdaterCLI),
        'ssb-update': ('Second stage boot update tool', SSBUpdaterCLI),
        'fw-config': ('Firmware configuration tool', FWConfigCommandCLI),
        'udp-rate-limiter': ('Limit the UDP rate', UDPRateLimiterCLI),
        'fw-control': ('Useful firmware control operations', ControlCommandCLI),
        'fw-logger': ('Download fw logs to a file', LoggerCommandCLI),
        'scan': ('Scans for devices (Ethernet or PCIE)', ScanCommandCLI),
        'sensor-config': ('Sensor configuration tool', SensorConfigCommandCLI),
        'run': ('Run a compiled network', RunCommandCLI),
        'benchmark': ('Measure basic performance on compiled network', BenchmarkCommandCLI),
        'monitor': ("Monitor of networks - Presents information about the running networks. To enable monitor, set in the application process the environment variable 'HAILO_MONITOR' to 1.", MonitorCommandCLI),
        'parse-hef': (' Parse HEF to get information about its components', ParseHEFCommandCLI),
        'measure-power': ('Measures power consumption', MeasurePowerCommandCLI),
        'tutorial': ('Runs the tutorials in jupyter notebook', TutorialRunnerCLI),
    }

    def __init__(self):
        self.parser = argparse.ArgumentParser(description=self._get_generic_description())
        self.parser.register('action', 'custom_version', CustomVersionAction)
        self.parser.add_argument('--version', action='custom_version')
        self.subparsers = self.parser.add_subparsers(help='Hailo utilities aimed to help with everything you need')
        self.COMMANDS = {}
        self.COMMANDS.update(type(self).PLATFORM_COMMANDS)

        try:
            # If sdk_client is importable - add its commands to this tool
            from hailo_sdk_client.tools.cmd_utils.main import ClientCommands
            self.COMMANDS.update(ClientCommands.SDK_COMMANDS)
        except ImportError:
            pass

    @staticmethod
    def _get_description():
        return 'Hailo Platform SW v{} command line utilities'.format(hailo_platform.__version__)

    @staticmethod
    def _get_generic_description():
        return 'Hailo Command Line Utility'

    def run(self):
        argv = sys.argv[1:]
        return self._run(argv)

    # Dependency injection for testing
    def _run(self, argv):
        self.COMMANDS['help'] = ('show the list of commands', Helper(self.COMMANDS))

        # Create the commands and let them set the arguments
        commands = {}
        for command_name, (help_, command_class) in self.COMMANDS.items():
            commands[command_name] = command_class(self.subparsers.add_parser(command_name, help=help_))

        argcomplete.autocomplete(self.parser)
        # HailortCliUtil commands are parsed by hailortcli:
        # * argv from after the command name are forwarded to hailortcli.
        # * E.g. calling `hailo udp-limiter arg1 arg2` will result in the call `commands['udp-limiter'].run(['arg1', 'arg2'])`.
        #   Inturn, this will create the process `hailortcli udp-rate-limiter arg1 arg2` (via the UDPRateLimiterCLI class).
        # In order to support this, we first check if the first argument in argv is the name of a HailortCliUtil class.
        # If so, we'll pass the arguments to the HailortCliUtil, otherwise we parse with argparse as usual.
        if len(argv) == 0:
            self.parser.print_help()
            return self.INVALID_COMMAND_EXIT_CODE

        command_name = argv[0]
        if (command_name in commands) and isinstance(commands[command_name], HailortCliUtil):
            # HailortCliUtil just passes the rest of the argv to hailortcli
            try:
                return commands[command_name].run(argv[1:])
            except HailortCliUtilError as e:
                print('\n' + str(e))
                return

        # This isn't a HailortCliUtil commnad, parse with argparse
        args = self.parser.parse_args(argv)
        # Due to a bug in Python's handling of subparsers, we cannot require a sub-command using argparse. A manual
        # check is performed here so we can print out a proper message. (https://bugs.python.org/issue9253)
        if not vars(args):
            self.parser.print_help()
            return self.INVALID_COMMAND_EXIT_CODE

        # The commands themself, set the default func.
        return args.func(args)


def main():
    a = PlatformCommands()
    return a.run()


if __name__ == '__main__':
    main()
