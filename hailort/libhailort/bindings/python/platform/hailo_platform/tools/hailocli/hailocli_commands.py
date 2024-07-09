import os
import pathlib
import subprocess
import sys

import importlib.util

import hailo_platform
from hailo_platform.tools.hailocli.base_utils import HailortCliUtil

"""
    HailoRTCLI matching commands in Hailo-CLI tool.
"""


class BenchmarkCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'benchmark')


class FWConfigCommandCLI(HailortCliUtil):
    """CLI tool for changing the FW configuration (User Config)"""

    def __init__(self, parser):
        super().__init__(parser, 'fw-config')


class BoardConfigCommandCLI(HailortCliUtil):
    """CLI tool for changing the FW configuration (Board Config)"""

    def __init__(self, parser):
        super().__init__(parser, 'board-config')


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


class RunCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'run')


class SensorConfigCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'sensor-config')


class FWUpdaterCLI(HailortCliUtil):
    """Cli tool for firmware updates"""

    def __init__(self, parser):
        super().__init__(parser, 'fw-update')


class SSBUpdaterCLI(HailortCliUtil):
    """Cli tool for second stage boot updates"""

    def __init__(self, parser):
        super().__init__(parser, 'ssb-update')


class UDPRateLimiterCLI(HailortCliUtil):
    """CLI tool for UDP rate limitation."""

    def __init__(self, parser):
        super().__init__(parser, 'udp-rate-limiter')


class TutorialRequired(Exception):
    pass


class TutorialRunnerCLI():
    TUTORIALS_DIR = os.path.join(pathlib.Path(hailo_platform.__file__).parent.parent, 'hailo_tutorials/notebooks/')
    TUTORIALS_REQUIREMENTS = ["jupyter"]
    ERROR_MSG = """
    Jupyter or one of its dependencies are not installed in this Python environment. These packages
    are not mandatory for pyHailoRT itself, but they are required in order to run the Python API tutorial
    notebooks. Please run the following command to install the missing Python packages: 
    """

    def __init__(self, parser):
        parser.add_argument('--ip', type=str, default=None, help='the ip parameter passed to jupyter-notebook.')
        parser.add_argument('--port', type=str, default=None, help='the port parameter passed to jupyter-notebook.')
        parser.set_defaults(func=self.run)

    def _check_requirements(self):
        missing_pkgs = []
        for req in self.TUTORIALS_REQUIREMENTS:
            if importlib.util.find_spec(req) is None:
                missing_pkgs.append(req)

        if missing_pkgs:
            sys.tracebacklimit = 0
            raise TutorialRequired(f"\n{self.ERROR_MSG}\n    {'; '.join([f'pip install {pkg}' for pkg in missing_pkgs])}")

    def run(self, args):
        self._check_requirements()
        exec_string = f'jupyter-notebook {self.TUTORIALS_DIR}'
        if args.ip is not None:
            exec_string += f" --ip={args.ip}"

        if args.port is not None:
            exec_string += f" --port={args.port}"

        return subprocess.run(
            exec_string,
            shell=True,
            check=True,
            env=os.environ.copy(),
        )


class ParseHEFCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'parse-hef')


class MonitorCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'monitor')
