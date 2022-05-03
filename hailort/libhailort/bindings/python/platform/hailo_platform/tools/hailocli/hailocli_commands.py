
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