#!/usr/bin/env python
from enum import Enum

from hailo_platform.tools.hailocli.base_utils import CmdUtilsBaseUtil
from hailo_platform.pyhailort.hw_object import EthernetDevice, PcieDevice
from hailo_platform.common.logger.logger import default_logger
from hailo_platform.pyhailort.pyhailort import PcieDeviceInfo, InternalPcieDevice

logger = default_logger()

class HailoCLITargets(Enum):
    udp = 'udp'
    pcie = 'pcie'


class HailoDeviceCmdUtil(CmdUtilsBaseUtil):
    """
    Base class for any cmd utility that use a specific hailo device
    """
    def __init__(self, args_parser, set_target_args=True):
        super().__init__(args_parser)
        self._parser = args_parser
        if set_target_args:
            self.add_target_args(args_parser)

    def get_target(self, args):
        self.validate_args(args)
        target_type = self.get_target_type(args)
        if target_type == HailoCLITargets.udp.value:
            return EthernetDevice(args.ip)
        else:
            try:
                return PcieDevice(device_info=args.board_location)
            except Exception as e:
                logger.error('Internal PCIe error (No PCIe device connected?)')
                logger.error('Error code: {}'.format(e))
                raise

    def get_target_type(self, args):
        if args.target is not None:
            return args.target

        if args.ip is not None:
            # If IP is given, we assume that the target is udp
            return HailoCLITargets.udp.value

        # Otherwise the default target is pcie
        return HailoCLITargets.pcie.value


    def validate_args(self, args):
        if args.board_location:
            all_devices = InternalPcieDevice.scan_devices()
            if args.board_location not in all_devices:
                self._parser.error('Device {} does not appear on your host, please run `hailo scan -d pcie` to see all available devices'
                    .format(args.board_location))

    def add_target_args(self, args_parser):
        args_parser.add_argument('--ip', type=str, default=None, help='IP address of the target (udp)')
        args_parser.add_argument('-s', '--board-location', help=PcieDeviceInfo.BOARD_LOCATION_HELP_STRING,
                                 type=PcieDeviceInfo.argument_type)
