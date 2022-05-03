import pkg_resources
# hailo_platform package has been renamed to hailort, but the import is still hailo_platform
__version__ = pkg_resources.get_distribution("hailort").version

import sys

from hailo_platform.pyhailort.hailo_control_protocol import BoardInformation, CoreInformation, HailoResetTypes, ExtendedDeviceInformation, HealthInformation

from argparse import ArgumentTypeError
import numpy
import signal
import time
from hailo_platform.common.logger.logger import default_logger
import gc
import os

import hailo_platform.pyhailort._pyhailort as _pyhailort
if _pyhailort.__version__ != __version__:
    raise ImportError("_pyhailort version ({}) does not match pyhailort version ({})".format(_pyhailort.__version__, __version__))

from hailo_platform.pyhailort._pyhailort import (BootloaderVersion, TemperatureInfo, # noqa F401
                                                        DvmTypes, PowerMeasurementTypes,  # noqa F401
                                                        PowerMeasurementData, NotificationId,  # noqa F401
                                                        OvercurrentAlertState,
                                                        FormatOrder,
                                                        AveragingFactor, SamplingPeriod, MeasurementBufferIndex,
                                                        FormatType, WatchdogMode,
                                                        MipiDataTypeRx, MipiPixelsPerClock,
                                                        MipiClockSelection, MipiIspImageInOrder,
                                                        MipiIspImageOutDataType, IspLightFrequency,
                                                        BootSource, HailoSocketDefs, Endianness,
                                                        MipiInputStreamParams, SensorConfigTypes,
                                                        SensorConfigOpCode)

BBOX_PARAMS = _pyhailort.HailoRTDefaults.BBOX_PARAMS()
HAILO_DEFAULT_ETH_CONTROL_PORT = _pyhailort.HailoRTDefaults.HAILO_DEFAULT_ETH_CONTROL_PORT()
INPUT_DATAFLOW_BASE_PORT = _pyhailort.HailoRTDefaults.DEVICE_BASE_INPUT_STREAM_PORT()
OUTPUT_DATAFLOW_BASE_PORT = _pyhailort.HailoRTDefaults.DEVICE_BASE_OUTPUT_STREAM_PORT()
PCIE_ANY_DOMAIN = _pyhailort.HailoRTDefaults.PCIE_ANY_DOMAIN()
DEFAULT_VSTREAM_TIMEOUT_MS = 10000
DEFAULT_VSTREAM_QUEUE_SIZE = 2

class HailoSocket(object):
    MAX_UDP_PAYLOAD_SIZE = HailoSocketDefs.MAX_UDP_PAYLOAD_SIZE()
    MIN_UDP_PAYLOAD_SIZE = HailoSocketDefs.MIN_UDP_PAYLOAD_SIZE()
    MAX_UDP_PADDED_PAYLOAD_SIZE = HailoSocketDefs.MAX_UDP_PADDED_PAYLOAD_SIZE()
    MIN_UDP_PADDED_PAYLOAD_SIZE = HailoSocketDefs.MIN_UDP_PADDED_PAYLOAD_SIZE()
    MAX_ALIGNED_UDP_PAYLOAD_SIZE_RTP = HailoSocketDefs.MAX_ALIGNED_UDP_PAYLOAD_SIZE_RTP()


class HailoRTException(Exception):
    pass

class UdpRecvError(HailoRTException):
    pass

class InvalidProtocolVersionException(HailoRTException):
    pass

class HailoRTFirmwareControlFailedException(HailoRTException):
    pass

class HailoRTInvalidFrameException(HailoRTException):
    pass

class HailoRTUnsupportedOpcodeException(HailoRTException):
    pass

class HailoRTTimeout(HailoRTException):
    pass

class HailoRTStreamAborted(HailoRTException):
    pass

class HailoRTInvalidOperationException(HailoRTException):
    pass

class HailoRTInvalidArgumentException(HailoRTException):
    pass

class HailoRTNotFoundException(HailoRTException):
    pass

class HailoRTInvalidHEFException(HailoRTException):
    pass

class HailoRTEthException(HailoRTException):
    pass

class HailoRTPCIeDriverException(HailoRTException):
    pass

class HailoRTNetworkGroupNotActivatedException(HailoRTException):
    pass

class HailoStatusInvalidValueException(Exception):
    pass

class ExceptionWrapper(object):
    def __enter__(self):
        pass

    def __exit__(self, exception_type, value, traceback):
        if value is not None:
            if exception_type is _pyhailort.HailoRTStatusException:
                self._raise_indicative_status_exception(int(value.args[0]))
            else:
                raise

    def _raise_indicative_status_exception(self, error_code):
        string_error_code = get_status_message(error_code)
        if string_error_code == "HAILO_ETH_RECV_FAILURE":
            raise UdpRecvError("Failed to receive data")
        if string_error_code == "HAILO_UNSUPPORTED_CONTROL_PROTOCOL_VERSION":
            raise InvalidProtocolVersionException("HailoRT has failed because an invalid protocol version was received from device")
        if string_error_code == "HAILO_FW_CONTROL_FAILURE":
            raise HailoRTFirmwareControlFailedException("libhailort control operation failed")
        if string_error_code == "HAILO_UNSUPPORTED_OPCODE":
            raise HailoRTUnsupportedOpcodeException("HailoRT has failed because an unsupported opcode was sent to device")
        if string_error_code == "HAILO_INVALID_FRAME":
            raise HailoRTInvalidFrameException("An invalid frame was received")
        if string_error_code == "HAILO_TIMEOUT":
            raise HailoRTTimeout("Received a timeout - hailort has failed because a timeout had occurred")
        if string_error_code == "HAILO_STREAM_ABORTED":
            raise HailoRTStreamAborted("Stream aborted due to an external event")

        if string_error_code == "HAILO_INVALID_OPERATION":
            raise HailoRTInvalidOperationException("Invalid operation. See hailort.log for more information")
        if string_error_code == "HAILO_INVALID_ARGUMENT":
            raise HailoRTInvalidArgumentException("Invalid argument. See hailort.log for more information")
        if string_error_code == "HAILO_NOT_FOUND":
            raise HailoRTNotFoundException("Item not found. See hailort.log for more information")

        if string_error_code == "HAILO_INVALID_HEF":
            raise HailoRTInvalidHEFException("Invalid HEF. See hailort.log for more information")

        if string_error_code == "HAILO_ETH_FAILURE":
            raise HailoRTEthException("Ethernet failure. See hailort.log for more information")
        if string_error_code == "HAILO_PCIE_DRIVER_FAIL":
            raise HailoRTPCIeDriverException("PCIe driver failure. run 'dmesg | grep hailo' for more information")

        if string_error_code == "HAILO_NETWORK_GROUP_NOT_ACTIVATED":
            raise HailoRTNetworkGroupNotActivatedException("Network group is not activated")
        else:
            raise HailoRTException("libhailort failed with error: {} ({})".format(error_code, string_error_code))

def get_status_message(status_code):
    status_str = _pyhailort.get_status_message(status_code)
    if status_str == "":
        raise HailoStatusInvalidValueException("Value {} is not a valid status".format(status_code))
    return status_str

class Control(object):
    class Type(object):
        PCIE = 0
        ETH = 1
    
    def __init__(self, control_type, address, port, pcie_device_info=None, response_timeout_seconds=10,
     max_number_of_attempts=3):
        self.device = None
        self.control_type = control_type
        self._eth_address = address
        self._eth_port = port
        self._eth_response_timeout_milliseconds = int(response_timeout_seconds * 1000)
        self._eth_max_number_of_attempts = max_number_of_attempts
        self._pcie_device_info = pcie_device_info

        if sys.platform != "win32":
            signal.pthread_sigmask(signal.SIG_BLOCK, [signal.SIGWINCH])

    def ensure_device(method):
        def _ensure_device(self, *args, **kw):
            if self.device is not None:
                return method(self, *args, **kw)
            
            with ExceptionWrapper():
                if self.control_type == Control.Type.PCIE:
                    self.device = _pyhailort.Device.create_pcie(self._pcie_device_info)
                elif self.control_type == Control.Type.ETH:
                    self.device = _pyhailort.Device.create_eth(self._eth_address, self._eth_port,
                        self._eth_response_timeout_milliseconds, self._eth_max_number_of_attempts)
                else:
                    raise HailoRTException("Unsupported control type")
            try:
                result = method(self, *args, **kw)
            finally:
                if self.device is not None:
                    with ExceptionWrapper():
                        self.device.release()
                self.device = None

            return result
        return _ensure_device
    
    def set_device(self, device_object):
        if device_object is None:
            self.device = None
        else:
            self.device = device_object.device
        
    @ensure_device
    def identify(self):
        with ExceptionWrapper():
            response = self.device.identify()
        board_information = BoardInformation(response.protocol_version, response.fw_version.major,
            response.fw_version.minor, response.fw_version.revision, response.logger_version,
            response.board_name, response.is_release,  int(response.device_architecture), response.serial_number,
            response.part_number, response.product_name)
        return board_information

    @ensure_device
    def core_identify(self):
        with ExceptionWrapper():
            response = self.device.core_identify()
        core_information = CoreInformation(response.fw_version.major, response.fw_version.minor, 
            response.fw_version.revision, response.is_release)
        return core_information

    @ensure_device
    def set_fw_logger(self, level, interface_mask):
        with ExceptionWrapper():
            return self.device.set_fw_logger(level, interface_mask)

    @ensure_device
    def set_throttling_state(self, should_activate):
        with ExceptionWrapper():
            return self.device.set_throttling_state(should_activate)

    @ensure_device
    def get_throttling_state(self):
        with ExceptionWrapper():
            return self.device.get_throttling_state()

    @ensure_device
    def _set_overcurrent_state(self, should_activate):
        with ExceptionWrapper():
            return self.device._set_overcurrent_state(should_activate)

    @ensure_device
    def _get_overcurrent_state(self):
        with ExceptionWrapper():
            return self.device._get_overcurrent_state()

    @ensure_device
    def read_memory(self, address, length):
        with ExceptionWrapper():
            return self.device.read_memory(int(address), int(length))

    @ensure_device
    def write_memory(self, address, data):
        with ExceptionWrapper():
            return self.device.write_memory(int(address), data, len(data))
    
    @ensure_device
    def configure(self, hef, configure_params_by_name={}):
        with ExceptionWrapper():
            return self.device.configure(hef._hef, configure_params_by_name)

    @ensure_device
    def _create_c_i2c_slave(self, pythonic_slave):
        c_slave = _pyhailort.I2CSlaveConfig()
        c_slave.endianness = pythonic_slave.endianness
        c_slave.slave_address = pythonic_slave.slave_address
        c_slave.register_address_size = pythonic_slave.register_address_size
        c_slave.bus_index = pythonic_slave.bus_index
        return c_slave

    @ensure_device
    def i2c_write(self, pythonic_slave, register_address, data):
        c_slave = self._create_c_i2c_slave(pythonic_slave)
        with ExceptionWrapper():
            return self.device.i2c_write(c_slave, register_address, data, len(data))

    @ensure_device
    def i2c_read(self, pythonic_slave, register_address, data_length):
        c_slave = self._create_c_i2c_slave(pythonic_slave)
        with ExceptionWrapper():
            return self.device.i2c_read(c_slave, register_address, data_length)

    @ensure_device
    def power_measurement(self, dvm, measurement_type):
        with ExceptionWrapper():
            return self.device.power_measurement(dvm, measurement_type)
    
    @ensure_device
    def start_power_measurement(self, averaging_factor, sampling_period):
        with ExceptionWrapper():
            return self.device.start_power_measurement(averaging_factor, sampling_period)
    
    @ensure_device
    def set_power_measurement(self, buffer_index, dvm, measurement_type):
        with ExceptionWrapper():
            return self.device.set_power_measurement(buffer_index, dvm, measurement_type)

    @ensure_device
    def get_power_measurement(self, buffer_index, should_clear):
        with ExceptionWrapper():
            return self.device.get_power_measurement(buffer_index, should_clear)

    @ensure_device
    def stop_power_measurement(self):
        with ExceptionWrapper():
            return self.device.stop_power_measurement()
    
    @ensure_device
    def examine_user_config(self):
        with ExceptionWrapper():
            return self.device.examine_user_config()

    @ensure_device
    def read_user_config(self):
        with ExceptionWrapper():
            return self.device.read_user_config()
    
    @ensure_device
    def write_user_config(self, data):
        with ExceptionWrapper():
            return self.device.write_user_config(data)
    
    @ensure_device
    def erase_user_config(self):
        with ExceptionWrapper():
            return self.device.erase_user_config()

    @ensure_device
    def read_board_config(self):
        with ExceptionWrapper():
            return self.device.read_board_config()

    @ensure_device
    def write_board_config(self, data):
        with ExceptionWrapper():
            return self.device.write_board_config(data)

    @ensure_device
    def reset(self, reset_type):
        map_mode = {
            HailoResetTypes.CHIP    : _pyhailort.ResetDeviceMode.CHIP,
            HailoResetTypes.NN_CORE : _pyhailort.ResetDeviceMode.NN_CORE,
            HailoResetTypes.SOFT : _pyhailort.ResetDeviceMode.SOFT,
            HailoResetTypes.FORCED_SOFT : _pyhailort.ResetDeviceMode.FORCED_SOFT
            }
        
        mode = map_mode[reset_type]
        with ExceptionWrapper():
            return self.device.reset(mode)

    @ensure_device
    def sensor_store_config(self, section_index, reset_data_size, sensor_type, config_file_path, config_height, config_width, 
                            config_fps, config_name):
        with ExceptionWrapper():
            return self.device.sensor_store_config(section_index, reset_data_size, sensor_type, config_file_path, 
                config_height, config_width, config_fps, config_name)

    @ensure_device
    def store_isp_config(self, reset_config_size, config_height, config_width, config_fps, isp_static_config_file_path, 
                                isp_runtime_config_file_path, config_name):
        with ExceptionWrapper():
            return self.device.store_isp_config(reset_config_size, config_height, config_width, config_fps, 
                isp_static_config_file_path, isp_runtime_config_file_path, config_name)

    @ensure_device
    def sensor_get_sections_info(self):
        with ExceptionWrapper():
            return self.device.sensor_get_sections_info()

    @ensure_device
    def sensor_set_i2c_bus_index(self, sensor_type, bus_index):
        with ExceptionWrapper():
            return self.device.sensor_set_i2c_bus_index(sensor_type, bus_index)
        
    @ensure_device
    def sensor_load_and_start_config(self, section_index):
        with ExceptionWrapper():
            return self.device.sensor_load_and_start_config(section_index)

    @ensure_device
    def sensor_reset(self, section_index):
        with ExceptionWrapper():
            return self.device.sensor_reset(section_index)

    @ensure_device
    def sensor_set_generic_i2c_slave(self, slave_address, register_address_size, bus_index, should_hold_bus, endianness):
        with ExceptionWrapper():
            return self.device.sensor_set_generic_i2c_slave(slave_address, register_address_size, bus_index, should_hold_bus, endianness)

    @ensure_device
    def firmware_update(self, firmware_binary, should_reset):
        with ExceptionWrapper():
            return self.device.firmware_update(firmware_binary, len(firmware_binary), should_reset)

    @ensure_device
    def second_stage_update(self, second_stage_binary):
        with ExceptionWrapper():
            return self.device.second_stage_update(second_stage_binary, len(second_stage_binary))

    @ensure_device
    def set_pause_frames(self, rx_pause_frames_enable):
        with ExceptionWrapper():
            return self.device.set_pause_frames(rx_pause_frames_enable)

    @ensure_device
    def wd_enable(self, cpu_id):
        with ExceptionWrapper():
            return self.device.wd_enable(cpu_id)

    @ensure_device
    def wd_disable(self, cpu_id):
        with ExceptionWrapper():
            return self.device.wd_disable(cpu_id)

    @ensure_device
    def wd_config(self, cpu_id, wd_cycles, wd_mode):
        with ExceptionWrapper():
            return self.device.wd_config(cpu_id, wd_cycles, WatchdogMode(wd_mode))

    @ensure_device
    def previous_system_state(self, cpu_id):
        with ExceptionWrapper():
            return self.device.previous_system_state(cpu_id)

    @ensure_device
    def get_chip_temperature(self):
        with ExceptionWrapper():
            return self.device.get_chip_temperature()

    @ensure_device
    def get_extended_device_information(self):
        with ExceptionWrapper():
            response = self.device.get_extended_device_information()
        device_information = ExtendedDeviceInformation(response.neural_network_core_clock_rate,
            response.supported_features, response.boot_source, response.lcs, response.soc_id,  response.eth_mac_address , response.unit_level_tracking_id, response.soc_pm_values)
        return device_information

    @ensure_device
    def _get_health_information(self):
        with ExceptionWrapper():
            response = self.device._get_health_information()
        health_information = HealthInformation(response.overcurrent_protection_active, response.current_overcurrent_zone, response.red_overcurrent_threshold,
                    response.orange_overcurrent_threshold, response.temperature_throttling_active, response.current_temperature_zone, response.current_temperature_throttling_level, 
                    response.temperature_throttling_levels, response.orange_temperature_threshold, response.orange_hysteresis_temperature_threshold,
                    response.red_temperature_threshold, response.red_hysteresis_temperature_threshold)
        return health_information
    
    @ensure_device
    def set_notification_callback(self, callback_func, notification_id, opaque):
        with ExceptionWrapper():
            self.device.set_notification_callback(callback_func, notification_id, opaque)

    @ensure_device
    def remove_notification_callback(self, notification_id):
        with ExceptionWrapper():
            self.device.remove_notification_callback(notification_id)

    @ensure_device
    def test_chip_memories(self):
        """
        Test chip memories using smart BIST mechanism.
        """
        with ExceptionWrapper():
            return self.device.test_chip_memories()

    @ensure_device
    def _get_device_handle(self):
        return self.device


class HailoUdpScan(object):
    def __init__(self):
        self._logger = default_logger()
        with ExceptionWrapper():
            self._scan = _pyhailort.UdpScan()

    def scan_devices(self, interface_name, timeout_seconds=3):
        self._logger.info('Scanning over interface {iface}'.format(iface=interface_name))
        timeout_milliseconds = int(timeout_seconds * 1000)
        device_ip_addresses =  self._scan.scan_devices(interface_name, timeout_milliseconds)
        for ip in device_ip_addresses:
            self._logger.debug("Found board at: {}".format(ip))
        return device_ip_addresses


class TrafficControl(object):
    def __init__(self, ip, port, rate_bytes_per_sec):
        if sys.platform != 'linux':
            raise HailoRTInvalidOperationException('TrafficControl is supported only on UNIX os')
        with ExceptionWrapper():
            self._tc_util = _pyhailort.TrafficControlUtil(ip, port, int(rate_bytes_per_sec))
    
    def set_rate_limit(self):
        self._tc_util.set_rate_limit()
    
    def reset_rate_limit(self):
        self._tc_util.reset_rate_limit()

    def get_interface_name(ip):
        "get the interface corresponding to the given ip"
        with ExceptionWrapper():
            return _pyhailort.TrafficControlUtil.get_interface_name(ip)


class ConfigureParams(object):

    @staticmethod
    def create_from_hef(hef, interface):
        """Create configure params from HEF. These params affects the HEF configuration into a device.

        Args:
            hef (:class:`HEF`): The HEF to create the parameters from.
            interface (:class:`HailoStreamInterface`): The stream_interface to create stream_params for.

        Returns:
            dict: The created stream params. The keys are the network_group names in the HEF. The values are default params, which can be changed.
        """
        with ExceptionWrapper():
            return hef._hef.create_configure_params(interface)

    @staticmethod
    def create_mipi_inputs_from_hef(hef, output_interface, mipi_rx_id=0, data_type=MipiDataTypeRx.RAW_8,
            img_width_pixels=1920, img_height_pixels=1080,
            pixels_per_clock=MipiPixelsPerClock.PIXELS_PER_CLOCK_4, number_of_lanes=2,
            clock_selection=MipiClockSelection.SELECTION_AUTOMATIC, data_rate=260, virtual_channel_index=0,
            isp_enable=False, isp_img_in_order=MipiIspImageInOrder.GR_FIRST,
            isp_img_out_data_type=MipiIspImageOutDataType.RGB_888, isp_crop_enable=False,
            isp_crop_output_width_pixels=1920, isp_crop_output_height_pixels=1080,
            isp_crop_output_width_start_offset_pixels=0, isp_crop_output_height_start_offset_pixels=0,
            isp_test_pattern_enable=True, isp_configuration_bypass=False,
            isp_run_time_ae_enable=True, isp_run_time_awb_enable=True, isp_run_time_adt_enable=True,
            isp_run_time_af_enable=False, isp_run_time_calculations_interval_ms=0,
            isp_light_frequency=IspLightFrequency.LIGHT_FREQ_50_HZ):
        """Create configure params from HEF. These params affects the HEF configuration into a device.

        .. attention:: The ISP and its features are not officially supported yet.

        Args:
            hef (:class:`HEF`): The HEF to create the parameters from.
            output_interface (:class:`HailoStreamInterface`): The stream_interface to create output stream_params for.
            mipi_rx_id (int): Selection of which MIPI Rx device to use.
            data_type (:class:`~hailo_platform.pyhailort.pyhailort.MipiDataTypeRx`): The data type which will be passed over the MIPI.
            img_width_pixels (int): The width in pixels of the image that enter to the mipi CSI. The sensor output.
                                        When isp_enable and isp_crop_enable is false, is also the stream input.
            img_height_pixels (int): The height in pixels of the image that enter to the mipi CSI. The sensor output.
                                        When isp_enable and isp_crop_enable is false, is also the stream input.
            pixels_per_clock (:class:`~hailo_platform.pyhailort.pyhailort.MipiPixelsPerClock`): Number of pixels transmitted at each
                clock.
            number_of_lanes (int): Number of lanes to use.
            clock_selection (:class:`~hailo_platform.pyhailort.pyhailort.MipiClockSelection`): Selection of clock range that would be
                used. Setting :class:`~hailo_platform.pyhailort.pyhailort.MipiClockSelection.SELECTION_AUTOMATIC` means that the
                clock selection is calculated from the data rate.
            data_rate (int): Rate of the passed data (MHz).
            virtual_channel_index (int): The virtual channel index of the MIPI dphy.
            isp_enable (bool): Enable the ISP block in the MIPI dataflow. The ISP is not supported yet.
            isp_img_in_order (:class:`~hailo_platform.pyhailort.pyhailort.MipiIspImageInOrder`):
                The ISP Rx bayer pixel order. Only relevant when the ISP is enabled.
            isp_img_out_data_type (:class:`~hailo_platform.pyhailort.pyhailort.MipiIspImageOutDataType`):
                The data type that the mipi will take out. Only relevant when the ISP is enabled.
            isp_crop_enable (bool): Enable the crop feature in the ISP. Only relevant when the ISP is enabled.
            isp_crop_output_width_pixels (int): The width in pixels of the output window that the ISP take out. The stream input.
                                        Useful when isp_crop_enable is True. Only relevant when the ISP is enabled.
            isp_crop_output_height_pixels (int): The height in pixels of the output window that the ISP take out. The stream input.
                                        Useful when isp_crop_enable is True. Only relevant when the ISP is enabled.
            isp_crop_output_width_start_offset_pixels (int): The width start point of the output window that the ISP take out. 
                                        Useful when isp_crop_enable is True. Only relevant when the ISP is enabled.
            isp_crop_output_height_start_offset_pixels (int): The height start point of the output window that the ISP take out. 
                                        Useful when isp_crop_enable is True. Only relevant when the ISP is enabled.
            isp_test_pattern_enable (bool): Enable Test pattern from the ISP. Only relevant when the ISP is enabled.
            isp_configuration_bypass (bool): Don't load the ISP configuration file from the FLASH. Only relevant when the ISP is enabled.
            isp_run_time_ae_enable (bool): Enable the run-time Auto Exposure in the ISP. Only relevant when the ISP is enabled.
            isp_run_time_awb_enable (bool): Enable the run-time Auto White Balance in the ISP. Only relevant when the ISP is enabled.
            isp_run_time_adt_enable (bool): Enable the run-time Adaptive Function in the ISP. Only relevant when the ISP is enabled.
            isp_run_time_af_enable (bool): Enable the run-time Auto Focus in the ISP. Only relevant when the ISP is enabled.
            isp_run_time_calculations_interval_ms (int): Interval in milliseconds between ISP run time calculations. Only relevant when the ISP is enabled.
            isp_light_frequency (:class:`~hailo_platform.pyhailort.pyhailort.IspLightFrequency`):
                                        Selection of the light frequency. This parameter varies depending on the power grid of the country where 
                                        the product is running. Only relevant when the ISP is enabled.
        Returns:
            dict: The created stream params. The keys are the network_group names in the HEF. The values are default params, which can be changed.
        """

        mipi_params = MipiInputStreamParams()
        mipi_params.mipi_rx_id = mipi_rx_id
        mipi_params.data_type = data_type
        mipi_params.isp_enable = isp_enable
        mipi_params.mipi_common_params.pixels_per_clock = pixels_per_clock
        mipi_params.mipi_common_params.number_of_lanes = number_of_lanes
        mipi_params.mipi_common_params.clock_selection = clock_selection
        mipi_params.mipi_common_params.virtual_channel_index = virtual_channel_index
        mipi_params.mipi_common_params.data_rate = data_rate
        mipi_params.mipi_common_params.img_width_pixels = img_width_pixels
        mipi_params.mipi_common_params.img_height_pixels = img_height_pixels
        mipi_params.isp_params.img_in_order = isp_img_in_order
        mipi_params.isp_params.img_out_data_type = isp_img_out_data_type
        mipi_params.isp_params.crop_enable = isp_crop_enable
        mipi_params.isp_params.crop_output_width_pixels = isp_crop_output_width_pixels
        mipi_params.isp_params.crop_output_height_pixels = isp_crop_output_height_pixels
        mipi_params.isp_params.crop_output_width_start_offset_pixels = isp_crop_output_width_start_offset_pixels
        mipi_params.isp_params.crop_output_height_start_offset_pixels = isp_crop_output_height_start_offset_pixels
        mipi_params.isp_params.test_pattern_enable = isp_test_pattern_enable
        mipi_params.isp_params.configuration_bypass = isp_configuration_bypass
        mipi_params.isp_params.run_time_ae_enable = isp_run_time_ae_enable
        mipi_params.isp_params.run_time_awb_enable = isp_run_time_awb_enable
        mipi_params.isp_params.run_time_adt_enable = isp_run_time_adt_enable
        mipi_params.isp_params.run_time_af_enable = isp_run_time_af_enable
        mipi_params.isp_params.isp_run_time_calculations_interval_ms = isp_run_time_calculations_interval_ms
        mipi_params.isp_params.isp_light_frequency = isp_light_frequency
        with ExceptionWrapper():
            return hef._hef.create_configure_params_mipi_input(output_interface, mipi_params)

def _get_name_as_str(name):
    return name if name is not None else ""

class HEF(object):
    """Python representation of the Hailo Executable Format, which contains one or more compiled
    models.
    """

    def __init__(self, hef_source):
        """Constructor for the HEF class.

        Args:
            hef_source (str or bytes): The source from which the HEF object will be created. If the
                source type is `str`, it is treated as a path to an hef file. If the source type is
                `bytes`, it is treated as a buffer. Any other type will raise a ValueError.
        """

        with ExceptionWrapper():
            if isinstance(hef_source, str):
                self._hef = _pyhailort.Hef.create_from_file(hef_source)
                self._path = hef_source
            elif isinstance(hef_source, bytes):
                self._hef = _pyhailort.Hef.create_from_buffer(hef_source)
                self._path = None
            else: 
                raise ValueError("HEF can only be created from a file path (str) or a buffer (bytes)")
        self._sorted_output_names = {}

    def get_networks_names(self, network_group_name=None):
        """Gets the names of all networks in a specific network group.

        Args:
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: The names of the networks.
        """
        name = _get_name_as_str(network_group_name)
        with ExceptionWrapper():
            return self._hef.get_networks_names(name)

    @property
    def path(self):
        """HEF file path."""
        return self._path
    
    def get_network_group_names(self):
        """Get the names of the network groups in this HEF."""
        with ExceptionWrapper():
            return self._hef.get_network_group_names()
  
    def get_network_groups_infos(self):
        """Get information about the network groups in this HEF."""
        with ExceptionWrapper():
            return self._hef.get_network_groups_infos()

    def get_input_vstream_infos(self, name=None):
        """Get input vstreams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input vstreams.
        """
        name = _get_name_as_str(name)
        return self._hef.get_input_vstream_infos(name)

    def get_output_vstream_infos(self, name=None):
        """Get output vstreams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all output vstreams
        """
        name = _get_name_as_str(name)
        return self._hef.get_output_vstream_infos(name)

    def get_all_vstream_infos(self, name=None):
        """Get input and output vstreams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input and output vstreams
        """
        name = _get_name_as_str(name)
        return self._hef.get_all_vstream_infos(name)

    def get_input_stream_infos(self, name=None):
        """Get the input low-level streams information.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all input low-level streams.
        """
        name = _get_name_as_str(name)
        return self._hef.get_input_stream_infos(name)


    def get_output_stream_infos(self, name=None):
        """Get the output low-level streams information of a specific network group.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all output low-level streams.
        """
        name = _get_name_as_str(name)
        return self._hef.get_output_stream_infos(name)

    def get_all_stream_infos(self, name=None):
        """Get input and output streams information of a specific network group.

        Args:
            name (str, optional): The name of the network or network_group to access. In case network_group name is given,
                Address all networks of the given network_group. In case not given, first network_group is addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with all the information objects of all input and output streams
        """
        name = _get_name_as_str(name)
        return self._hef.get_all_stream_infos(name)

    def get_sorted_output_names(self, network_group_name=None):
        """Get the names of the outputs in a network group. The order of names is determined by
        the SDK. If the network group is not given, the first one is used.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]

        if network_group_name not in self._sorted_output_names:
            with ExceptionWrapper():
                self._sorted_output_names[network_group_name] = self._hef.get_sorted_output_names(network_group_name)
        return self._sorted_output_names[network_group_name]

    def bottleneck_fps(self, network_group_name=None):
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            bottleneck_fps = self._hef.get_bottleneck_fps(network_group_name)
            if bottleneck_fps == 0:
                raise HailoRTException("bottleneck_fps is zero")
            return bottleneck_fps

    def get_udp_rates_dict(self, fps, max_supported_rate_bytes, network_group_name=None):
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_udp_rates_dict(network_group_name, fps, int(max_supported_rate_bytes))

    def get_vstream_name_from_original_name(self, original_name, network_group_name=None):
        """Get vstream name from original layer name for a specific network group.

        Args:
            original_name (str): The original layer name.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            str: the matching vstream name for the provided original name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_vstream_name_from_original_name(original_name, network_group_name)

    def get_original_names_from_vstream_name(self, vstream_name, network_group_name=None):
        """Get original names list from vstream name for a specific network group.

        Args:
            vstream_name (str): The stream name.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: all the matching original layers names for the provided vstream name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_original_names_from_vstream_name(vstream_name, network_group_name)

    def get_vstream_names_from_stream_name(self, stream_name, network_group_name=None):
        """Get vstream names list from their underlying stream name for a specific network group.

        Args:
            stream_name (str): The underlying stream name.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: All the matching vstream names for the provided stream name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_vstream_names_from_stream_name(stream_name, network_group_name)

    def get_stream_names_from_vstream_name(self, vstream_name, network_group_name=None):
        """Get stream name from vstream name for a specific network group.

        Args:
            vstream_name (str): The name of the vstreams.
            network_group_name (str, optional): The name of the network group to access. If not given, first network_group is addressed.

        Returns:
            list of str: All the underlying streams names for the provided vstream name.
        """
        if network_group_name is None:
            network_group_name = self.get_network_group_names()[0]
        with ExceptionWrapper():
            return self._hef.get_stream_names_from_vstream_name(vstream_name, network_group_name)


class ConfiguredNetwork(object):
    """Represents a network group loaded to the device."""

    def __init__(self, configured_network, target, hef):
        self._configured_network = configured_network
        self._target = target
        self._hef = hef

    def get_networks_names(self):
        return self._hef.get_networks_names(self.name)

    def activate(self, network_group_params=None):
        """Activate this network group in order to infer data through it.

        Args:
            network_group_params (:obj:`hailo_platform.pyhailort._pyhailort.ActivateNetworkGroupParams`, optional):
                Network group activation params. If not given, default params will be applied,

        Returns:
            :class:`ActivatedNetworkContextManager`: Context manager that returns the activated
            network group.
        """
        network_group_params = network_group_params or self.create_params()

        with ExceptionWrapper():
            return ActivatedNetworkContextManager(self,
                self._configured_network.activate(network_group_params),
                self._target, self._hef)

    def wait_for_activation(self, timeout_ms=None):
        """Block until activated, or until ``timeout_ms`` is passed.

        Args:
            timeout_ms (int, optional): Timeout value in milliseconds to wait for activation.
                Defaults to ``HAILO_INFINITE``.

        Raises:
            :class:`HailoRTTimeout`: In case of timeout.
        """
        MAX_INT = 0x7fffffff
        with ExceptionWrapper():
            if timeout_ms is None:
                timeout_ms = MAX_INT
            return self._configured_network.wait_for_activation(timeout_ms)

    @staticmethod
    def create_params():
        """Create activation params for network_group.

        Returns:
            :obj:`hailo_platform.pyhailort._pyhailort.ActivateNetworkGroupParams`.
        """
        return _pyhailort.ActivateNetworkGroupParams.default()

    @property
    def name(self):
        return self._configured_network.get_name()

    def get_output_shapes(self):
        name_to_shape = {vstream_info.name : vstream_info.shape for vstream_info in self.get_output_vstream_infos()}
        results = []
        for name in self.get_sorted_output_names():
            results.append(name_to_shape[name])
        return tuple(results)

    def get_sorted_output_names(self):
        return self._hef.get_sorted_output_names(self.name)

    def get_input_vstream_infos(self, network_name=None):
        """Get input vstreams information.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input vstreams
        """

        name = network_name if network_name is not None else self.name
        return self._hef.get_input_vstream_infos(name)

    def get_output_vstream_infos(self, network_name=None):
        """Get output vstreams information.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all output vstreams
        """

        name = network_name if network_name is not None else self.name
        return self._hef.get_output_vstream_infos(name)

    def get_all_vstream_infos(self, network_name=None):
        """Get input and output vstreams information.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.VStreamInfo`: with all the information objects of all input and output vstreams
        """

        name = network_name if network_name is not None else self.name
        return self._hef.get_all_vstream_infos(name)

    def get_input_stream_infos(self, network_name=None):
        """Get the input low-level streams information of a specific network group.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all input low-level streams.
        """

        name = network_name if network_name is not None else self.name
        return self._hef.get_input_stream_infos(name)

    def get_output_stream_infos(self, network_name=None):
        """Get the output low-level streams information of a specific network group.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            List of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with information objects
            of all output low-level streams.
        """

        name = network_name if network_name is not None else self.name
        return self._hef.get_output_stream_infos(name)

    def get_all_stream_infos(self, network_name=None):
        """Get input and output streams information of a specific network group.

        Args:
            network_name (str, optional): The name of the network to access. In case not given, all the networks in the network group will be addressed.

        Returns:
            list of :obj:`hailo_platform.pyhailort._pyhailort.StreamInfo`: with all the information objects of all input and output streams
        """

        name = network_name if network_name is not None else self.name
        return self._hef.get_all_stream_infos(name)

    def get_udp_rates_dict(self, fps, max_supported_rate_bytes):
        with ExceptionWrapper():
            return self._configured_network.get_udp_rates_dict(int(fps), int(max_supported_rate_bytes))

    def _create_input_vstreams(self, input_vstreams_params):
        return self._configured_network.InputVStreams(input_vstreams_params)

    def _create_output_vstreams(self, output_vstreams_params):
        return self._configured_network.OutputVStreams(output_vstreams_params)

    def get_stream_names_from_vstream_name(self, vstream_name):
        """Get stream name from vstream name for a specific network group.

        Args:
            vstream_name (str): The name of the vstreams.

        Returns:
            list of str: All the underlying streams names for the provided vstream name.
        """
        with ExceptionWrapper():
            return self._hef.get_stream_names_from_vstream_name(vstream_name, self.name)

    def get_vstream_names_from_stream_name(self, stream_name):
        """Get vstream names list from their underlying stream name for a specific network group.

        Args:
            stream_name (str): The underlying stream name.

        Returns:
            list of str: All the matching vstream names for the provided stream name.
        """
        with ExceptionWrapper():
            return self._hef.get_vstream_names_from_stream_name(stream_name, self.name)


class ActivatedNetworkContextManager(object):
    """A context manager that returns the activated network group upon enter."""

    def __init__(self, configured_network, activated_network, target, hef):
        self._configured_network = configured_network
        self._activated_network = activated_network
        self._target = target
        self._hef = hef

    def __enter__(self):
        with ExceptionWrapper():
            activated_network_group = ActivatedNetwork(self._configured_network, self._activated_network.__enter__(), self._target,
                self._hef)
        return activated_network_group
    
    def __exit__(self, *args):
        self._activated_network.__exit__(*args)


class ActivatedNetwork(object):
    """The network group that is currently activated for inference."""

    def __init__(self, configured_network, activated_network, target, hef):
        self._configured_network = configured_network
        self._activated_network = activated_network
        self._target = target
        self._hef = hef
        self._last_number_of_invalid_frames_read = 0
    
    @property
    def target(self):
        return self._target

    @property
    def name(self):
        return self._configured_network.name

    def get_number_of_invalid_frames(self, clear=True):
        """Returns number of invalid frames.

        Args:
            clear (bool): If set, the returned value will be the number of invalid frames read since the last call to this function.

        Returns:
            int: Number of invalid frames.
        """
        total_invalid_frames_count = self._activated_network.get_invalid_frames_count()
        if clear:
            value = total_invalid_frames_count - self._last_number_of_invalid_frames_read
        self._last_number_of_invalid_frames_read = total_invalid_frames_count
        return value if clear else total_invalid_frames_count

    def validate_all_frames_are_valid(self):
        """Validates that all of the frames so far are valid (no invalid frames)."""
        number_of_invalid_frames = self.get_number_of_invalid_frames()
        if number_of_invalid_frames != 0:
            raise HailoRTException("There are {} invalid frames.".format(number_of_invalid_frames))

    def get_sorted_output_names(self):
        return self._hef.get_sorted_output_names(self.name)

    def _get_intermediate_buffer(self, src_context_index, src_stream_index):
        with ExceptionWrapper():
            return self._activated_network.get_intermediate_buffer(src_context_index, src_stream_index)


class InferVStreams(object):
    """Pipeline that allows to call blocking inference, to be used as a context manager."""

    def __init__(self, configured_net_group, input_vstreams_params, output_vstreams_params,
        tf_nms_format=False):
        """Constructor for the InferVStreams class.

        Args:
            configured_net_group (:class:`ConfiguredNetwork`): The configured network group for
                which the pipeline is created.
            input_vstreams_params (dict from str to :class:`InputVStreamParams`): Params for the
                input vstreams in the pipeline. Only members of this dict will take part in the
                inference.
            output_vstreams_params (dict from str to :class:`OutputVStreamParams`): Params for the
                output vstreams in the pipeline. Only members of this dict will take part in the
                inference.
            tf_nms_format (bool, optional): indicates whether the returned nms outputs should be in
                Hailo format or TensorFlow format. Default is False (using Hailo format).

                * Hailo format -- list of :obj:`numpy.ndarray`. Each element represents the
                  detections (bboxes) for the class, and its shape is
                  ``[number_of_detections, BBOX_PARAMS]``
                * TensorFlow format -- :obj:`numpy.ndarray` of shape
                  ``[class_count, BBOX_PARAMS, detections_count]`` padded with empty bboxes.
        """

        self._logger = default_logger()
        self._configured_net_group = configured_net_group
        self._net_group_name = configured_net_group.name
        self._input_vstreams_params = input_vstreams_params
        self._output_vstreams_params = output_vstreams_params
        self._tf_nms_format = tf_nms_format
        self._total_time = None
        self._hw_time = None
        self._network_name_to_outputs = InferVStreams._get_network_to_outputs_mapping(configured_net_group)
        self._input_name_to_network_name = InferVStreams._get_input_name_to_network_mapping(configured_net_group)

    @staticmethod
    def _get_input_name_to_network_mapping(configured_net_group):
        input_name_to_network_mapping = {}
        for network_name in configured_net_group.get_networks_names():
            for input_vstream_info in configured_net_group.get_input_vstream_infos(network_name):
                input_name_to_network_mapping[input_vstream_info.name] = network_name
        return input_name_to_network_mapping

    @staticmethod
    def _get_network_to_outputs_mapping(configured_net_group):
        network_to_outputs_mapping = {}
        for network_name in configured_net_group.get_networks_names():
            network_to_outputs_mapping[network_name] = set()
            for output_vstream_info in configured_net_group.get_output_vstream_infos(network_name):
                network_to_outputs_mapping[network_name].add(output_vstream_info.name)
        return network_to_outputs_mapping

    def _make_output_buffers_and_infos(self, input_data, batch_size):
        output_buffers = {}
        output_buffers_info = {}
        already_seen_networks = set()
        for input_name in input_data.keys():
            network_name = self._input_name_to_network_name[input_name]
            if (network_name not in already_seen_networks) :
                already_seen_networks.add(network_name)
                for output_name in self._network_name_to_outputs[network_name]:
                    output_buffers_info[output_name] = OutputLayerUtils(self._configured_net_group._hef, output_name, self._infer_pipeline,
                        self._net_group_name)
                    output_tensor_info = output_buffers_info[output_name].output_tensor_info
                    shape, dtype = output_tensor_info
                    output_buffers[output_name] = numpy.empty([batch_size] + list(shape), dtype=dtype)
        return output_buffers, output_buffers_info

    def __enter__(self):
        self._infer_pipeline = _pyhailort.InferVStreams(self._configured_net_group._configured_network,
            self._input_vstreams_params, self._output_vstreams_params)
        return self
    
    def infer(self, input_data):
        """Run inference on the hardware device.

        Args:
            input_data (dict of :obj:`numpy.ndarray`): Where the key is the name of the input_layer,
                and the value is the data to run inference on.

        Returns:
            dict: Output tensors of all output layers. The keys are outputs names and the values
            are output data tensors as :obj:`numpy.ndarray` (or list of :obj:`numpy.ndarray` in case of nms output and tf_nms_format=False).
        """

        time_before_infer_calcs = time.time()
        if not isinstance(input_data, dict):
            input_stream_infos = self._configured_net_group.get_input_stream_infos()
            if len(input_stream_infos) != 1:
                raise Exception("when there is more than one input, the input_data should be of type dict,"
                                             " mapping between each input_name, and his input_data tensor. number of inputs: {}".format(len(input_stream_infos)))
            input_data = {input_stream_infos[0].name : input_data}

        batch_size = InferVStreams._get_number_of_frames(input_data)
        output_buffers, output_buffers_info = self._make_output_buffers_and_infos(input_data, batch_size)

        for input_layer_name in input_data:
            # TODO: Remove cast after tests are updated and are working
            self._cast_input_data_if_needed(input_layer_name, input_data)
            self._validate_input_data_format_type(input_layer_name, input_data)
            self._make_c_contiguous_if_needed(input_layer_name, input_data)

        with ExceptionWrapper():
            time_before_infer = time.time()
            self._infer_pipeline.infer(input_data, output_buffers, batch_size)
            self._hw_time = time.time() - time_before_infer

        for name, result_array in output_buffers.items():
            is_nms = output_buffers_info[name].is_nms
            if not is_nms:
                continue
            nms_shape = output_buffers_info[name].vstream_info.nms_shape
            if self._tf_nms_format:
                shape = [batch_size] + output_buffers_info[name].old_nms_fomrat_shape
                output_dtype = output_buffers_info[name].output_dtype
                quantized_empty_bbox = output_buffers_info[name].quantized_empty_bbox
                flat_result_array = result_array.reshape(-1)
                output_buffers[name] = HailoRTTransformUtils.output_raw_buffer_to_nms_tf_format(flat_result_array, shape,
                    output_dtype, quantized_empty_bbox)
            else:
                output_buffers[name] = HailoRTTransformUtils.output_raw_buffer_to_nms_format(result_array, nms_shape.number_of_classes)
        
        self._total_time = time.time() - time_before_infer_calcs
        return output_buffers

    def get_hw_time(self):
        """Get the hardware device operation time it took to run inference over the last batch.

        Returns:
            float: Time in seconds.
        """
        return self._hw_time

    def get_total_time(self):
        """Get the total time it took to run inference over the last batch.

        Returns:
            float: Time in seconds.
        """
        return self._total_time

    def _cast_input_data_if_needed(self, input_layer_name, input_data):
        input_dtype = input_data[input_layer_name].dtype
        with ExceptionWrapper():
            input_expected_dtype = self._infer_pipeline.get_host_dtype(input_layer_name)
        if input_dtype != input_expected_dtype:

            self._logger.warning("Given input data dtype ({}) is different than inferred dtype ({}). "
                "conversion for every frame will reduce performance".format(input_dtype,
                    input_expected_dtype))
            input_data[input_layer_name] = input_data[input_layer_name].astype(input_expected_dtype)
    
    def _validate_input_data_format_type(self, input_layer_name, input_data):
        if input_layer_name not in self._input_vstreams_params:
            return

        input_data_format = self._input_vstreams_params[input_layer_name].user_buffer_format
        input_expected_item_size = _pyhailort.get_format_data_bytes(input_data_format)
        input_item_size = input_data[input_layer_name].dtype.itemsize

        # TODO: Add distinction between float32 and int32 and others
        if input_item_size != input_expected_item_size:
            raise HailoRTException("{} numpy array item size is {}, not {}".format(input_layer_name,
                input_item_size, input_expected_item_size))

    @staticmethod
    def _get_number_of_frames(input_data):
        # Checks that all the batch-sizes of the input_data are equals for all input layers
        if len(input_data) == 0:
            raise ValueError("Input_data can't be empty")
        batch_size_of_first_input = list(input_data.values())[0].shape[0]
        for name, input_data_tensor in input_data.items():
            if input_data_tensor.shape[0] != batch_size_of_first_input:
                raise ValueError(
                    "The number of frames on all input_tensors should be equal! different sizes detected: {} != {}".format(
                        batch_size_of_first_input, input_data_tensor.shape[0]))
        return batch_size_of_first_input

    def _make_c_contiguous_if_needed(self, input_layer_name, input_data):
        if not input_data[input_layer_name].flags.c_contiguous:
            self._logger.warning("Converting {} numpy array to be C_CONTIGUOUS".format(
                input_layer_name))
            input_data[input_layer_name] = numpy.asarray(input_data[input_layer_name], order='C')

    def __exit__(self, *args):
        self._infer_pipeline.release()
        return False


class HailoRTTransformUtils(object):
    @staticmethod
    def get_dtype(data_bytes):
        """Get data type from the number of bytes."""
        if data_bytes == 1:
            return numpy.uint8
        elif data_bytes == 2:
            return numpy.uint16
        elif data_bytes == 4:
            return numpy.float32
        raise HailoRTException("unsupported data bytes value")

    @staticmethod
    def dequantize_output_buffer(src_buffer, dst_buffer, elements_count, quant_info):
        """De-quantize the data in input buffer `src_buffer` and output it to the buffer `dst_buffer`

        Args:
            src_buffer (:obj:`numpy.ndarray`): The input buffer containing the data to be de-quantized.
                The buffer's data type is the source data type.
            dst_buffer (:obj:`numpy.ndarray`): The buffer that will contain the de-quantized data.
                The buffer's data type is the destination data type.
            elements_count (int): The number of elements to de-quantize. This number must not exceed 'src_buffer' or 'dst_buffer' sizes.
            quant_info (:class:`~hailo_platform.pyhailort.pyhailort.QuantInfo`): The quantization info.
        """
        with ExceptionWrapper():
            src_format_type = HailoRTTransformUtils._get_format_type(src_buffer.dtype)
            dst_format_type = HailoRTTransformUtils._get_format_type(dst_buffer.dtype)
            _pyhailort.dequantize_output_buffer(src_buffer, dst_buffer, src_format_type, dst_format_type, elements_count, quant_info)

    @staticmethod
    def dequantize_output_buffer_in_place(raw_buffer, dst_dtype, elements_count, quant_info):
        """De-quantize the output buffer `raw_buffer` to data type `dst_dtype`.

        Args:
            raw_buffer (:obj:`numpy.ndarray`): The output buffer to be de-quantized. The buffer's data type is the source data type.
            dst_dtype (:obj:`numpy.dtype`): The data type to de-quantize `raw_buffer` to.
            elements_count (int): The number of elements to de-quantize. This number must not exceed 'raw_buffer' size.
            quant_info (:class:`~hailo_platform.pyhailort.pyhailort.QuantInfo`): The quantization info.
        """
        with ExceptionWrapper():
            src_format_type = HailoRTTransformUtils._get_format_type(raw_buffer.dtype)
            dst_format_type = HailoRTTransformUtils._get_format_type(dst_dtype)
            _pyhailort.dequantize_output_buffer_in_place(raw_buffer, src_format_type, dst_format_type, elements_count, quant_info)

    @staticmethod
    def quantize_input_buffer(src_buffer, dst_buffer, elements_count, quant_info):
        """Quantize the data in input buffer `src_buffer` and output it to the buffer `dst_buffer`

        Args:
            src_buffer (:obj:`numpy.ndarray`): The input buffer containing the data to be quantized.
                The buffer's data type is the source data type.
            dst_buffer (:obj:`numpy.ndarray`): The buffer that will contain the quantized data.
                The buffer's data type is the destination data type.
            elements_count (int): The number of elements to quantize. This number must not exceed 'src_buffer' or 'dst_buffer' sizes.
            quant_info (:class:`~hailo_platform.pyhailort.pyhailort.QuantInfo`): The quantization info.
        """
        with ExceptionWrapper():
            src_format_type = HailoRTTransformUtils._get_format_type(src_buffer.dtype)
            dst_format_type = HailoRTTransformUtils._get_format_type(dst_buffer.dtype)
            _pyhailort.quantize_input_buffer(src_buffer, dst_buffer, src_format_type, dst_format_type, elements_count, quant_info)

    @staticmethod
    def output_raw_buffer_to_nms_tf_format(raw_output_buffer, shape, dtype, quantized_empty_bbox):
        offset = 0
        # We create the tf_format buffer with reversed width/features for preformance optimization
        converted_output_buffer = numpy.empty([shape[0], shape[1], shape[3], shape[2]], dtype=dtype)
        for frame in range(converted_output_buffer.shape[0]):
            offset = frame * converted_output_buffer.shape[1] * (converted_output_buffer.shape[2] * converted_output_buffer.shape[3] + 1)
            HailoRTTransformUtils.output_raw_buffer_to_nms_tf_format_single_frame(raw_output_buffer, converted_output_buffer[frame],
                converted_output_buffer.shape[1], converted_output_buffer.shape[2], quantized_empty_bbox, offset)
        converted_output_buffer = numpy.swapaxes(converted_output_buffer, 2, 3)
        return converted_output_buffer

    @staticmethod
    def output_raw_buffer_to_nms_tf_format_single_frame(raw_output_buffer, converted_output_frame, number_of_classes,
        max_bboxes_per_class, quantized_empty_bbox, offset=0):
        for class_i in range(number_of_classes):
            class_bboxes_amount = int(raw_output_buffer[offset])
            offset += 1
            if 0 != class_bboxes_amount:
                converted_output_frame[class_i][ : class_bboxes_amount][:] = raw_output_buffer[offset : offset + (BBOX_PARAMS * class_bboxes_amount)].reshape(class_bboxes_amount, BBOX_PARAMS)
                offset += BBOX_PARAMS * class_bboxes_amount
            converted_output_frame[class_i][class_bboxes_amount : max_bboxes_per_class][:] = quantized_empty_bbox

    @staticmethod
    def output_raw_buffer_to_nms_format(raw_output_buffer, number_of_classes):
        converted_output_buffer = []
        for frame in raw_output_buffer:
            converted_output_buffer.append(HailoRTTransformUtils.output_raw_buffer_to_nms_format_single_frame(frame, number_of_classes))
        return converted_output_buffer

    @staticmethod
    def output_raw_buffer_to_nms_format_single_frame(raw_output_buffer, number_of_classes, offset=0):
        converted_output_frame = []
        for class_i in range(number_of_classes):
            class_bboxes_amount = int(raw_output_buffer[offset])
            offset += 1
            if class_bboxes_amount == 0:
                converted_output_frame.append(numpy.empty([0, BBOX_PARAMS]))
            else:
                converted_output_frame.append(raw_output_buffer[offset : offset + (BBOX_PARAMS * class_bboxes_amount)].reshape(
                    class_bboxes_amount, BBOX_PARAMS))
                offset += BBOX_PARAMS * class_bboxes_amount
        return converted_output_frame

    @staticmethod
    def _get_format_type(dtype):
        if dtype == numpy.uint8:
            return FormatType.UINT8
        elif dtype == numpy.uint16:
            return FormatType.UINT16
        elif dtype == numpy.float32:
            return FormatType.FLOAT32
        raise HailoRTException("unsupported data type {}".format(dtype))

class InternalEthernetDevice(object):
    def __init__(self, address, port, response_timeout_seconds=10, max_number_of_attempts=3):
        self.device = None
        self._address = address
        self._port = port
        self._response_timeout_milliseconds = int(response_timeout_seconds * 1000)
        self._max_number_of_attempts = max_number_of_attempts
        with ExceptionWrapper():
            self.device = _pyhailort.Device.create_eth(self._address, self._port,
                self._response_timeout_milliseconds, self._max_number_of_attempts)

    def __del__(self):
        self.release()

    def release(self):
        if self.device is None:
            return
        with ExceptionWrapper():
            self.device.release()
            self.device = None


class PcieDeviceInfo(_pyhailort.PcieDeviceInfo):
    """Represents pcie device info, includeing domain, bus, device and function.
    """

    BOARD_LOCATION_HELP_STRING = 'Board location in the format of the command: "lspci -d 1e60: | cut -d\' \' -f1" ([<domain>]:<bus>:<device>.<func>). If not specified the first board is taken.'

    def __init__(self, bus, device, func, domain=None):
        super(PcieDeviceInfo, self).__init__()
        self.bus = bus
        self.device = device
        self.func = func
        if domain is None:
            self.domain = PCIE_ANY_DOMAIN
        else:
            self.domain = domain

    def __eq__(self, other):
        return (self.domain, self.bus, self.device, self.func) == (other.domain, other.bus, other.device, other.func)

    def __str__(self):
        with ExceptionWrapper():
            return super().__str__()

    def __repr__(self):
        return 'PcieDeviceInfo({})'.format(str(self))

    @classmethod
    def from_string(cls, board_location_str):
        """Parse pcie device info BDF from string. The format is [<domain>]:<bus>:<device>.<func>"""
        with ExceptionWrapper():
            device_info = _pyhailort.PcieDeviceInfo._parse(board_location_str)
            return PcieDeviceInfo(device_info.bus, device_info.device, device_info.func, device_info.domain)

    @classmethod
    def argument_type(cls, board_location_str):
        """PcieDeviceInfo Argument type for argparse parsers"""
        try:
            return cls.from_string(board_location_str)
        except HailoRTException:
            raise ArgumentTypeError('Invalid device info string, format is [<domain>]:<bus>:<device>.<func>')


class InternalPcieDevice(object):
    def __init__(self, device_info=None):
        self.device = None
        if device_info is None:
            device_info = InternalPcieDevice.scan_devices()[0]
        self._device_info = device_info
        with ExceptionWrapper():
            self.device = _pyhailort.Device.create_pcie(self._device_info)

    def __del__(self):
        self.release()

    def release(self):
        if self.device is None:
            return
        with ExceptionWrapper():
            self.device.release()
            self.device = None

    @staticmethod
    def scan_devices():
        with ExceptionWrapper():
            return [PcieDeviceInfo(dev_info.bus, dev_info.device, dev_info.func, dev_info.domain)
                for dev_info in _pyhailort.scan_pcie_devices()]

    def create_debug_log(self):
        return PcieDebugLog(self)

    def write_memory(self, address, data):
        with ExceptionWrapper():
            self.device.direct_write_memory(address, data)

    def read_memory(self, address, size):
        with ExceptionWrapper():
            return self.device.direct_read_memory(address, size)


class PcieDebugLog(object):
    def __init__(self, pci_device):
        self._pcie_device = pci_device

    def read(self, count, cpu_id):
        with ExceptionWrapper():
            return self._pcie_device.device.read_log(count, cpu_id)


class HailoPowerMeasurementUtils(object):
    @staticmethod
    def return_real_sampling_period(sampling_period):
        """Get a sampling period from the enum."""
        SamplingPeriodDictionary = dict([
                (SamplingPeriod.PERIOD_140us, 140),
                (SamplingPeriod.PERIOD_204us, 204),
                (SamplingPeriod.PERIOD_332us, 332),
                (SamplingPeriod.PERIOD_588us, 588),
                (SamplingPeriod.PERIOD_1100us, 1100),
                (SamplingPeriod.PERIOD_2116us, 2116),
                (SamplingPeriod.PERIOD_4156us, 4156),
                (SamplingPeriod.PERIOD_8244us, 8244),
        ])
        return SamplingPeriodDictionary[sampling_period]

    @staticmethod
    def return_real_averaging_factor(averaging_factor):
        """Get an averaging factor from the enum."""
        AveragingFactorDictionary = dict([
                (AveragingFactor.AVERAGE_1, 1),
                (AveragingFactor.AVERAGE_4, 4),
                (AveragingFactor.AVERAGE_16, 16),
                (AveragingFactor.AVERAGE_64, 64),
                (AveragingFactor.AVERAGE_128, 128),
                (AveragingFactor.AVERAGE_256, 256),
                (AveragingFactor.AVERAGE_512, 512),
                (AveragingFactor.AVERAGE_1024, 1024),
        ])
        return AveragingFactorDictionary[averaging_factor]

class HailoPowerMode(_pyhailort.PowerMode):
    pass

class HailoStreamInterface(_pyhailort.StreamInterface):
    pass

class HailoStreamDirection(_pyhailort.StreamDirection):
    pass

class HailoCpuId(_pyhailort.CpuId):
    pass

class HailoFormatFlags(_pyhailort.FormatFlags):
    pass


class VDevice(object):
    """Hailo virtual device representation."""

    def __init__(
            self,
            params=None, device_infos=None):

        """Create the Hailo virtual device object.

        Args:
            params (:obj:`hailo_platform.pyhailort.pyhailort.VDeviceParams`, optional): VDevice params, call
                :func:`VDevice.create_params` to get default params. Excludes 'device_infos'.
            device_infos (list of :obj:`hailo_platform.pyhailort.pyhailort.PcieDeviceInfo`, optional): pcie devices infos to create VDevice from,
                call :func:`PcieDevice.scan_devices` to get list of all available devices. Excludes 'params'.
        """
        gc.collect()
        self._id = "VDevice"
        self._params = params
        self._device_infos = device_infos
        if self._device_infos is not None:
            if self._params is not None:
                raise HailoRTException("VDevice can be created from params or device_infos. Both parameters was passed to the c'tor")
        self._vdevice = None
        self._loaded_network_groups = []
        self._open_vdevice()

        self._creation_pid = os.getpid()

    def _open_vdevice(self):
        if self._device_infos is not None:
            with ExceptionWrapper():
                self._vdevice = _pyhailort.VDevice.create_from_infos(self._device_infos)
        else:
            if self._params is None:
                self._params = VDevice.create_params()
            with ExceptionWrapper():
                self._vdevice = _pyhailort.VDevice.create(self._params)

    def __enter__(self):
        return self

    def release(self):
        """Release the allocated resources of the device. This function should be called when working with the device not as context-manager."""
        if self._vdevice is not None:
            self._vdevice.release()
            self._vdevice = None

    def __exit__(self, *args):
        self.release()
        return False

    def __del__(self):
        self.release()

    @staticmethod
    def create_params():
        with ExceptionWrapper():
            return _pyhailort.VDeviceParams.default()

    def configure(self, hef, configure_params_by_name={}):
        """Configures target vdevice from HEF object.

        Args:
            hef (:class:`~hailo_platform.pyhailort.pyhailort.HEF`): HEF to configure the vdevice from
            configure_params_by_name (dict, optional): Maps between each net_group_name to configure_params. If not provided, default params will be applied
        """
        if self._creation_pid != os.getpid():
            raise HailoRTException("VDevice can only be configured from the process it was created in.")
        with ExceptionWrapper():
            configured_apps = self._vdevice.configure(hef._hef, configure_params_by_name)
        configured_networks = [ConfiguredNetwork(configured_app, self, hef) for configured_app in configured_apps]
        self._loaded_network_groups.extend(configured_networks)
        return configured_networks

    def get_physical_devices(self):
        """Gets the underlying physical devices.

        Return:
            list of :obj:`~hailo_platform.pyhailort.hw_object.PcieDevice`: The underlying physical devices.
        """
        with ExceptionWrapper():
            phys_dev_infos = self._vdevice.get_physical_devices_infos()
        pythonic_dev_infos = [PcieDeviceInfo(dev_info.bus, dev_info.device, dev_info.func, dev_info.domain)
            for dev_info in phys_dev_infos]

        from hailo_platform.pyhailort.hw_object import PcieDevice
        return [PcieDevice(info) for info in pythonic_dev_infos]

    def get_physical_devices_infos(self):
        """Gets the physical devices infos.

        Return:
            list of :obj:`~hailo_platform.pyhailort.pyhailort.PcieDeviceInfo`: The underlying physical devices infos.
        """
        with ExceptionWrapper():
            return self._vdevice.get_physical_devices_infos()


class InputVStreamParams(object):
    """Parameters of an input virtual stream (host to device)."""

    @staticmethod
    def make(configured_network, quantized=True, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create input virtual stream params from a configured network group. These params determine the format of the
        data that will be fed into the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized (bool): Whether the data fed into the chip is already quantized. True means
                the data is already quantized. False means it's HailoRT's responsibility to quantize
                (scale) the data. Defaults to True.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all input virtual streams. If quantized is False,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.FLOAT32`. Otherwise,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the data is fed in the same format expected by the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all input virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        if format_type is None:
            if not quantized:
                format_type = FormatType.FLOAT32
            else:
                format_type = FormatType.AUTO
        if timeout_ms is None:
            timeout_ms = DEFAULT_VSTREAM_TIMEOUT_MS
        if queue_size is None:
            queue_size = DEFAULT_VSTREAM_QUEUE_SIZE
        name = network_name if network_name is not None else configured_network.name
        with ExceptionWrapper():
            return configured_network._hef._hef.get_input_vstreams_params(name, quantized,
                format_type, timeout_ms, queue_size)

    @staticmethod
    def make_from_network_group(configured_network, quantized=True, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create input virtual stream params from a configured network group. These params determine the format of the
        data that will be fed into the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized (bool): Whether the data fed into the chip is already quantized. True means
                the data is already quantized. False means it's HailoRT's responsibility to quantize
                (scale) the data. Defaults to True.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all input virtual streams. If quantized is False,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.FLOAT32`. Otherwise,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the data is fed in the same format expected by the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all input virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        return InputVStreamParams.make(configured_network, quantized, format_type, timeout_ms, queue_size, network_name)


class OutputVStreamParams(object):
    """Parameters of an output virtual stream (device to host)."""

    @staticmethod
    def make(configured_network, quantized=True, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create output virtual stream params from a configured network group. These params determine the format of the
        data that will be fed into the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized (bool): Whether the data fed into the chip is already quantized. True means
                the data is already quantized. False means it's HailoRT's responsibility to quantize
                (scale) the data. Defaults to True.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all output virtual streams. If quantized is False,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.FLOAT32`. Otherwise,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the data is fed in the same format expected by the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all output virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        if format_type is None:
            if not quantized:
                format_type = FormatType.FLOAT32
            else:
                format_type = FormatType.AUTO
        if timeout_ms is None:
            timeout_ms = DEFAULT_VSTREAM_TIMEOUT_MS
        if queue_size is None:
            queue_size = DEFAULT_VSTREAM_QUEUE_SIZE
        name = network_name if network_name is not None else configured_network.name
        with ExceptionWrapper():
            return configured_network._hef._hef.get_output_vstreams_params(name, quantized,
                format_type, timeout_ms, queue_size)

    @staticmethod
    def make_from_network_group(configured_network, quantized=True, format_type=None, timeout_ms=None, queue_size=None, network_name=None):
        """Create output virtual stream params from a configured network group. These params determine the format of the
        data that will be fed into the network group.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized (bool): Whether the data fed into the chip is already quantized. True means
                the data is already quantized. False means it's HailoRT's responsibility to quantize
                (scale) the data. Defaults to True.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all output virtual streams. If quantized is False,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.FLOAT32`. Otherwise,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the data is fed in the same format expected by the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all output virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.
            network_name (str): Network name of the requested virtual stream params.
                If not passed, all the networks in the network group will be addressed.

        Returns:
            dict: The created virtual streams params. The keys are the vstreams names. The values are the
            params.
        """
        return OutputVStreamParams.make(configured_network, quantized, format_type, timeout_ms, queue_size, network_name)

    @staticmethod
    def make_groups(configured_network, quantized=True, format_type=None, timeout_ms=None, queue_size=None):
        """Create output virtual stream params from a configured network group. These params determine the format of the
        data that will be fed into the network group. The params groups are splitted with respect to their underlying streams for multi process usges.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the params are created.
            quantized (bool): Whether the data fed into the chip is already quantized. True means
                the data is already quantized. False means it's HailoRT's responsibility to quantize
                (scale) the data. Defaults to True.
            format_type (:class:`~hailo_platform.pyhailort.pyhailort.FormatType`): The
                default format type of the data for all output virtual streams. If quantized is False,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.FLOAT32`. Otherwise,
                the default is :attr:`~hailo_platform.pyhailort.pyhailort.FormatType.AUTO`,
                which means the data is fed in the same format expected by the device (usually
                uint8).
            timeout_ms (int): The default timeout in milliseconds for all output virtual streams.
                Defaults to DEFAULT_VSTREAM_TIMEOUT_MS. In case of timeout, :class:`HailoRTTimeout` will be raised.
            queue_size (int): The pipeline queue size. Defaults to DEFAULT_VSTREAM_QUEUE_SIZE.

        Returns:
            list of dicts: Each element in the list represent a group of params, where the keys are the vstreams names, and the values are the
            params. The params groups are splitted with respect to their underlying streams for multi process usges.
        """
        all_params = OutputVStreamParams.make(configured_network, quantized=quantized, format_type=format_type, timeout_ms=timeout_ms, queue_size=queue_size)
        low_level_streams_names = [stream_info.name for stream_info in configured_network.get_output_stream_infos()]
        stream_name_to_vstream_names = {stream_name: configured_network.get_vstream_names_from_stream_name(stream_name) for stream_name in low_level_streams_names}
        results = []
        for low_level_stream_name, vstream_names in stream_name_to_vstream_names.items():
            params_group = {}
            for vstream_name in vstream_names:
                # Vstreams that were already seen should not be added to another params_group
                if all_params[vstream_name] is not None:
                    params_group[vstream_name] = all_params[vstream_name]
                    all_params[vstream_name] = None
            if 0 < len(params_group):
                results.append(params_group)
        return results


class InputVStream(object):
    """Represents a single virtual stream in the host to device direction."""

    def __init__(self, send_object):
        self._send_object = send_object
        self._input_dtype = self._send_object.dtype

    @property
    def shape(self):
        return self._send_object.shape
    
    @property
    def dtype(self):
        return self._send_object.dtype

    @property
    def name(self):
        return self._send_object.info.name

    @property
    def network_name(self):
        return self._send_object.info.network_name

    def send(self, input_data):
        """Send frames to inference.

        Args:
            input_data (:obj:`numpy.ndarray`): Data to run inference on.
        """
        
        if input_data.dtype != self._input_dtype:
            input_data = input_data.astype(self._input_dtype)

        if not input_data.flags.c_contiguous:
            logger = default_logger()
            logger.warning("Warning - Converting input numpy array to be C_CONTIGUOUS")
            input_data = numpy.asarray(input_data, order='C')

        batch_number = 0
        batch_size = 1
        while batch_number < input_data.shape[0]:
            data = input_data[batch_number:batch_number + batch_size]
            with ExceptionWrapper():
                self._send_object.send(data)
            batch_number += batch_size

    def flush(self):
        """Blocks until there are no buffers in the input VStream pipeline."""
        with ExceptionWrapper():
            self._send_object.flush()

    @property
    def info(self):
        with ExceptionWrapper():
            return self._send_object.info

class InputVStreams(object):
    """Input vstreams pipelines that allows to send data, to be used as a context manager."""

    def __init__(self, configured_network, input_vstreams_params):
        """Constructor for the InputVStreams class.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which the pipeline is created.
            input_vstreams_params (dict from str to :class:`InputVStreamParams`): Params for the input vstreams in the pipeline.
        """
        self._configured_network = configured_network
        self._input_vstreams_params = input_vstreams_params
        self._vstreams = {}

    def __enter__(self):
        self._input_vstreams_holder = self._configured_network._create_input_vstreams(self._input_vstreams_params)
        self._input_vstreams_holder.__enter__()
        for name, vstream in self._input_vstreams_holder.get_all_inputs().items():
            self._vstreams[name] = InputVStream(vstream)
        return self

    def get(self, name=None):
        """Return a single input vstream by its name.
        
        Args:
            name (str): The vstream name. If name=None and there is a single input vstream, that single (:class:`InputVStream`) will be returned.
                Otherwise, if name=None and there are multiple input vstreams, an exception will be thrown.

        Returns:
            :class:`InputVStream`: The (:class:`InputVStream`) that corresponds to the given name.
        """
        if name is None:
            if len(self._vstreams) != 1:
                raise HailoRTException("There is no single input vStream. You must give a name")
            name = list(self._vstreams.keys())[0]
        return self._vstreams[name]

    def clear(self):
        """Clears the vstreams' pipeline buffers."""
        with ExceptionWrapper():
            self._input_vstreams_holder.clear()

    def __exit__(self, *args):
        self._input_vstreams_holder.__exit__(*args)
        return False
    
    def __iter__(self):
        return iter(self._vstreams.values())

class OutputLayerUtils(object):
    def __init__(self, hef, vstream_name, pipeline, net_group_name=""):
        self._hef = hef
        self._net_group_name = net_group_name
        self._vstream_info = self._get_vstream_info(vstream_name)

        if isinstance(pipeline, (_pyhailort.InferVStreams)):
            # TODO: HRT-5754 - Save user buffer instead of dtype and flags.
            self._output_dtype = pipeline.get_host_dtype(vstream_name)
            self._output_shape = pipeline.get_shape(vstream_name)
            self._output_flags = pipeline.get_user_buffer_format(vstream_name).flags
        else:
            self._output_dtype = pipeline.dtype
            self._output_shape = pipeline.shape
            self._output_flags = pipeline.get_user_buffer_format().flags

        self._is_nms = (self._vstream_info.format.order == FormatOrder.HAILO_NMS)
        if self._is_nms:
            self._quantized_empty_bbox = numpy.asarray([0] * BBOX_PARAMS, dtype=self._output_dtype)
            if not (self._output_flags & _pyhailort.FormatFlags.QUANTIZED):
                HailoRTTransformUtils.dequantize_output_buffer_in_place(self._quantized_empty_bbox, self._output_dtype,
                    BBOX_PARAMS, self._vstream_info.quant_info)
    
    @property
    def output_dtype(self):
        return self._output_dtype
    
    @property
    def output_shape(self):
        return self._output_shape

    @property
    def vstream_info(self):
        return self._vstream_info
    
    @property
    def output_tensor_info(self):
        return self.output_shape, self.output_dtype
    
    @property
    def is_nms(self):
        return self._is_nms
    
    @property
    def quantized_empty_bbox(self):
        return self._quantized_empty_bbox

    def _get_vstream_info(self, name):
        output_vstream_infos = self._hef.get_output_vstream_infos(self._net_group_name)
        for info in output_vstream_infos:
            if info.name == name:
                return info
        raise HailoRTException("No vstream matches the given name {}".format(name))

    @property
    def old_nms_fomrat_shape(self):
        nms_shape = self._vstream_info.nms_shape
        return [nms_shape.number_of_classes, BBOX_PARAMS,
                nms_shape.max_bboxes_per_class]

class OutputVStream(object):
    """Represents a single output virtual stream in the device to host direction."""

    def __init__(self, configured_network, recv_object, name, tf_nms_format=False, net_group_name=""):
        self._recv_object = recv_object
        self._output_layer_utils = OutputLayerUtils(configured_network._hef, name, self._recv_object, net_group_name)
        self._output_dtype = self._output_layer_utils.output_dtype
        self._vstream_info = self._output_layer_utils._vstream_info
        self._output_tensor_info = self._output_layer_utils.output_tensor_info
        self._is_nms = self._output_layer_utils.is_nms
        if self._is_nms:
            self._quantized_empty_bbox = self._output_layer_utils.quantized_empty_bbox
        self._tf_nms_format = tf_nms_format

    @property
    def shape(self):
        return self._recv_object.shape

    @property
    def dtype(self):
        return self._recv_object.dtype

    @property
    def name(self):
        return self._vstream_info.name

    @property
    def network_name(self):
        return self._vstream_info.network_name

    def recv(self):
        """Receive frames after inference.

        Returns:
            :obj:`numpy.ndarray`: The output of the inference for a single frame. The returned
            tensor does not include the batch dimension.
            In case of nms output and tf_nms_format=False, returns list of :obj:`numpy.ndarray`.
        """
        result_array = None
        with ExceptionWrapper():
            result_array = self._recv_object.recv()

        if self._is_nms:
            nms_shape = self._vstream_info.nms_shape
            if self._tf_nms_format:
                nms_results_tesnor = result_array
                # We create the tf_format buffer with reversed width/features for preformance optimization
                shape = self._output_layer_utils.old_nms_fomrat_shape
                result_array = numpy.empty([shape[0], shape[2], shape[1]], dtype=self._output_dtype)
                HailoRTTransformUtils.output_raw_buffer_to_nms_tf_format_single_frame(nms_results_tesnor, result_array,
                    nms_shape.number_of_classes,
                    nms_shape.max_bboxes_per_class, self._quantized_empty_bbox)
                result_array = numpy.swapaxes(result_array, 1, 2)
            else:
                result_array = HailoRTTransformUtils.output_raw_buffer_to_nms_format_single_frame(result_array,
                    nms_shape.number_of_classes)
        return result_array

    @property
    def info(self):
        with ExceptionWrapper():
            return self._recv_object.info

class OutputVStreams(object):
    """Output virtual streams pipelines that allows to receive data, to be used as a context manager."""

    def __init__(self, configured_network, output_vstreams_params, tf_nms_format=False):
        """Constructor for the OutputVStreams class.

        Args:
            configured_network (:class:`ConfiguredNetwork`): The configured network group for which
                the pipeline is created.
            output_vstreams_params (dict from str to :class:`OutputVStreamParams`): Params for the
                output vstreams in the pipeline.
            tf_nms_format (bool, optional): indicates whether the returned nms outputs should be in
                Hailo format or TensorFlow format. Default is False (using Hailo format).

                * Hailo format -- list of :obj:`numpy.ndarray`. Each element represents th
                  detections (bboxes) for the class, and its shape is
                  ``[number_of_detections, BBOX_PARAMS]``
                * TensorFlow format -- :obj:`numpy.ndarray` of shape
                  ``[class_count, BBOX_PARAMS, detections_count]`` padded with empty bboxes.
        """
        self._configured_network = configured_network
        self._net_group_name = configured_network.name
        self._output_vstreams_params = output_vstreams_params
        self._output_tensor_info = {}
        self._tf_nms_format = tf_nms_format
        self._vstreams = {}

    def __enter__(self):
        self._output_vstreams_holder = self._configured_network._create_output_vstreams(self._output_vstreams_params)
        self._output_vstreams_holder.__enter__()
        for name, vstream in self._output_vstreams_holder.get_all_outputs().items():
            self._vstreams[name] = OutputVStream(self._configured_network, vstream, name,
                tf_nms_format=self._tf_nms_format, net_group_name=self._net_group_name)
        return self

    def get(self, name=None):
        """Return a single output vstream by its name.
        
        Args:
            name (str): The vstream name. If name=None and there is a single output vstream, that single (:class:`OutputVStream`) will be returned.
                Otherwise, if name=None and there are multiple output vstreams, an exception will be thrown.

        Returns:
            :class:`OutputVStream`: The (:class:`OutputVStream`) that corresponds to the given name.
        """
        if name is None:
            if len(self._vstreams) != 1:
                raise HailoRTException("There is no single output vStream. You must give a name")
            name = list(self._vstreams.keys())[0]
        return self._vstreams[name]

    def clear(self):
        """Clears the vstreams' pipeline buffers."""
        with ExceptionWrapper():
            self._output_vstreams_holder.clear()

    def __exit__(self, *args):
        self._output_vstreams_holder.__exit__(*args)
        return False

    def __iter__(self):
        return iter(self._vstreams.values())