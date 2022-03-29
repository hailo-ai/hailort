from hailo_platform.common.tools.cmd_utils.base_utils import HailortCliUtil

class RunCommandCLI(HailortCliUtil):
    def __init__(self, parser):
        super().__init__(parser, 'run')
