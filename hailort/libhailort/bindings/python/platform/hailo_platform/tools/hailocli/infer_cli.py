from enum import Enum

from hailo_platform.tools.hailocli.hailo_device_utils import HailoDeviceCmdUtil
from hailo_platform.common.logger.logger import default_logger
from hailo_platform.tools.hailocli.hailocli_commands import RunCommandCLI
from hailo_platform.tools.hailocli.base_utils import HailortCliUtilError
logger = default_logger()

class InferModes(Enum):
    simple = 'simple'
    performance = 'performance'

class InferCLI(HailoDeviceCmdUtil):
    def __init__(self, parser):
        super().__init__(parser, set_target_args=False)
        self._hailortcli_run_command = RunCommandCLI(parser)
        self._parser = parser
        subparsers = parser.add_subparsers(title="Inference mode", dest="mode")
        subparsers.required = True
        simple_parser =subparsers.add_parser(InferModes.simple.value, help="'simple' mode is unsupported, please use 'hailo run' instead")
        simple_parser.add_argument('--input-data-path', type=str, default=None,
                                   help="unsupported argument.")
        simple_parser.add_argument('--results-path', type=str, default=None,
                                   help='Unsupported argument.')
        simple_parser.add_argument('--config-path', type=str, required=True, help='Path to config HEF to infer with')
        performance_parser = subparsers.add_parser(InferModes.performance.value,
            help="infer command is deprecated and will be removed in a future release, please use 'hailo run' instead")
        performance_parser.add_argument('--config-path', type=str, required=True,
                            help='Path to config HEF to infer with')
        self.add_target_args(performance_parser)
        performance_parser.add_argument('-t', '--streaming-time', type=float, default=10.0, help='For how long to stream in performance mode')
        performance_parser.add_argument('--streaming-mode',
                                        choices=['hw-only', 'full'],
                                        default='full',
                                        help='Whether to skip pre-infer and post-infer steps on host (hw-only) or do them (full)')
        parser.set_defaults(func=self.run)

    def run(self, args):
        if InferModes[args.mode] == InferModes.simple:
            logger.info("mode simple is deprecated please use \'hailo run\' instead.\n"
                ".npz and .npy format are unsupported, use binary file instead, you can use the following example:\n"
                "\'hailo run --input-files [Input file path] [hef]\'.\n"
                "for more information use \'hailo run --help\'.")
        else:
            self.validate_args(args)
            argv = [args.config_path, "-t", str(int(args.streaming_time)), '-m', "streaming" if args.streaming_mode == 'full' else 'hw_only']
            if args.target == 'udp':
                argv += ['-d', args.target, '--ip', args.ip]
            try:
                self._hailortcli_run_command.run(argv)
            except HailortCliUtilError as e:
                print('\n'+ str(e))
                return
