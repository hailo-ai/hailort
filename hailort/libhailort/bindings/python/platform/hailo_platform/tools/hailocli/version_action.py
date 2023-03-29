import argparse

import hailo_platform


class CustomVersionAction(argparse.Action):
    def __init__(self,
                 option_strings,
                 dest=argparse.SUPPRESS,
                 default=argparse.SUPPRESS,
                 help="show program's version number and exit"):
        super(CustomVersionAction, self).__init__(
            option_strings=option_strings,
            dest=dest,
            default=default,
            nargs=0,
            help=help)

    @staticmethod
    def _print_version():
        print(f'HailoRT v{hailo_platform.__version__}')

        try:
            import hailo_sdk_client
            print(f'Hailo Dataflow Compiler v{hailo_sdk_client.__version__}')
        except ImportError:
            pass

    def __call__(self, parser, namespace, values, option_string=None):
        self._print_version()
        parser.exit()
