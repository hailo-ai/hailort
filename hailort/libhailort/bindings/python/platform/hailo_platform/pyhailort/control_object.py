#!/usr/bin/env python

"""Control operations for the Hailo hardware device."""
import struct
import sys
import signal

from builtins import object
from abc import ABCMeta, abstractmethod
from future.utils import with_metaclass

from hailo_platform.common.logger.logger import default_logger

from hailo_platform.pyhailort.hailo_control_protocol import BoardInformation, CoreInformation, DeviceArchitectureTypes, ExtendedDeviceInformation, HealthInformation
from hailo_platform.pyhailort.power_measurement import SamplingPeriod, AveragingFactor, DvmTypes, PowerMeasurementTypes, MeasurementBufferIndex, _get_buffer_index_enum_member
from hailo_platform.pyhailort.pyhailort import InternalPcieDevice, ExceptionWrapper

import hailo_platform.pyhailort._pyhailort as _pyhailort


class ControlObjectException(Exception):
    """Raised on illegal ContolObject operation."""
    pass


class FirmwareUpdateException(Exception):
    pass


class HailoControl(with_metaclass(ABCMeta, object)):
    """Control object that sends control operations to a Hailo hardware device."""

    def __init__(self):
        """Initializes a new HailoControl object."""
        self._logger = default_logger()
        self._device = None

        if sys.platform != "win32":
            signal.pthread_sigmask(signal.SIG_BLOCK, [signal.SIGWINCH])

    @abstractmethod
    def open(self):
        """Initializes the resources needed for using a control device."""
        pass

    @abstractmethod
    def close(self):
        """Releases the resources that were allocated for the control device."""
        pass

    def configure(self, hef, configure_params_by_name={}):
        """
        Configures device from HEF object.

        Args:
            hef (:class:`~hailo_platform.pyhailort.pyhailort.HEF`): HEF to configure the
                device from.
            configure_params_by_name (dict, optional): Maps between each net_group_name to
                configure_params. In case of a mismatch with net_groups_names, default params will
                be used.
        """     
        with ExceptionWrapper():
            return self._device.configure(hef._hef, configure_params_by_name)

    @abstractmethod
    def chip_reset(self):
        """Resets the device (chip reset)."""
        pass

    @abstractmethod
    def read_memory(self, address, data_length):
        """Reads memory from the Hailo chip.
        Byte order isn't changed. The core uses little-endian byte order.

        Args:
            address (int): Physical address to read from.
            data_length (int): Size to read in bytes.

        Returns:
            list of str: Memory read from the chip, each index in the list is a byte.
        """
        pass

    @abstractmethod
    def write_memory(self, address, data_buffer):
        """Write memory to Hailo chip.
        Byte order isn't changed. The core uses little-endian byte order.

        Args:
            address (int): Physical address to write to.
            data_buffer (list of str): Data to write.
        """
        pass


class HcpControl(HailoControl):
    """Control object that uses the HCP protocol for controlling the device."""

    WORD_SIZE = 4


    def __init__(self):
        super(HcpControl, self).__init__()

    @property
    def device_id(self):
        """Getter for the device_id.

        Returns:
            str: A string ID of the device. BDF for PCIe devices, IP address for Ethernet devices, "Core" for core devices.
        """
        return self._device_id

    def open(self):
        """Initializes the resources needed for using a control device."""
        pass

    def close(self):
        """Releases the resources that were allocated for the control device."""
        pass

    def chip_reset(self):
        """Resets the device (chip reset)."""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.CHIP)

    def nn_core_reset(self):
        """Resets the nn_core."""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.NN_CORE)

    def soft_reset(self):
        """reloads the device firmware (soft reset)"""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.SOFT)
        
    def forced_soft_reset(self):
        """reloads the device firmware (forced soft reset)"""
        with ExceptionWrapper():
            return self._device.reset(_pyhailort.ResetDeviceMode.FORCED_SOFT)

    def read_memory(self, address, data_length):
        """Reads memory from the Hailo chip. Byte order isn't changed. The core uses little-endian
        byte order.

        Args:
            address (int): Physical address to read from.
            data_length (int): Size to read in bytes.

        Returns:
            list of str: Memory read from the chip, each index in the list is a byte
        """
        with ExceptionWrapper():
            return self._device.read_memory(address, data_length)

    def write_memory(self, address, data_buffer):
        """Write memory to Hailo chip. Byte order isn't changed. The core uses little-endian byte
        order.

        Args:
            address (int): Physical address to write to.
            data_buffer (list of str): Data to write.
        """
        with ExceptionWrapper():
            return self._device.write_memory(address, data_buffer, len(data_buffer))

    def power_measurement(self, dvm=DvmTypes.AUTO, measurement_type=PowerMeasurementTypes.AUTO):
        """Perform a single power measurement on an Hailo chip. It works with the default settings
        where the sensor returns a new value every 2.2 ms without averaging the values.

        Args:
            dvm (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes`):
                Which DVM will be measured. Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) will be different according to the board: \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for EVB is an approximation to the total power consumption of the chip in PCIe setups.
                 It sums :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.VDD_CORE`,
                 :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.MIPI_AVDD` and :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AVDD_H`.
                 Only :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER` can measured with this option. \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for platforms supporting current monitoring (such as M.2 and mPCIe): :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.OVERCURRENT_PROTECTION`
            measurement_type
             (:class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`):
             The type of the measurement.

        Returns:
            float: The measured power. \n
            For :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`: \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.SHUNT_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.BUS_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER`: Unit is W. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.CURRENT`: Unit is mA. \n


        Note:
            This function can perform measurements for more than just power. For all supported
            measurement types, please look at
            :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`.
        """
        if self.device_id.device_architecture != DeviceArchitectureTypes.HAILO8_B0:
            raise ControlObjectException("Invalid device architecture: {}".format(self.device_id.device_architecture))
        with ExceptionWrapper():
            return self._device.power_measurement(dvm, measurement_type)

    def start_power_measurement(self, delay=None, averaging_factor=AveragingFactor.AVERAGE_256, sampling_period=SamplingPeriod.PERIOD_1100us):
        """Start performing a long power measurement.

        Args:
            delay: Unused parameter. Will be removed in future versions.
            averaging_factor (:class:`~hailo_platform.pyhailort.pyhailort.AveragingFactor`):
                Number of samples per time period, sensor configuration value.
            sampling_period (:class:`~hailo_platform.pyhailort.pyhailort.SamplingPeriod`):
                Related conversion time, sensor configuration value. The sensor samples the power
                every ``sampling_period`` [ms] and averages every ``averaging_factor`` samples. The
                sensor provides a new value every: 2 * sampling_period * averaging_factor [ms]. The
                firmware wakes up every ``delay`` [ms] and checks the sensor. If there is a new`
                value to read from the sensor, the firmware reads it.  Note that the average
                calculated by the firmware is "average of averages", because it averages values
                that have already been averaged by the sensor.
        """
        # TODO: Remove deprecated arg
        if delay is not None:
            self._logger.warning("Passing 'delay' to 'start_power_measurement()' is deprecated and will be removed in future versions")
        with ExceptionWrapper():
            return self._device.start_power_measurement(averaging_factor, sampling_period)

    def stop_power_measurement(self):
        """Stop performing a long power measurement. Deletes all saved results from the firmware.
        Calling the function eliminates the start function settings for the averaging the samples,
        and returns to the default values, so the sensor will return a new value every 2.2 ms
        without averaging values.
        """
        with ExceptionWrapper():
            return self._device.stop_power_measurement()

    def set_power_measurement(self, buffer_index=MeasurementBufferIndex.MEASUREMENT_BUFFER_INDEX_0, dvm=DvmTypes.AUTO, measurement_type=PowerMeasurementTypes.AUTO):
        """Set parameters for long power measurement on an Hailo chip.

        Args:
            buffer_index (:class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex`): Index of the buffer on the firmware the data would be saved at.
                Default is :class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex.MEASUREMENT_BUFFER_INDEX_0`
            dvm (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes`):
                Which DVM will be measured. Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) will be different according to the board: \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for EVB is an approximation to the total power consumption of the chip in PCIe setups.
                 It sums :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.VDD_CORE`,
                 :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.MIPI_AVDD` and :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AVDD_H`.
                 Only :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER` can measured with this option. \n
                 Default (:class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.AUTO`) for platforms supporting current monitoring (such as M.2 and mPCIe): :class:`~hailo_platform.pyhailort.pyhailort.DvmTypes.OVERCURRENT_PROTECTION`
            measurement_type
             (:class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`):
             The type of the measurement.

        Note:
            This function can perform measurements for more than just power. For all supported measurement types
            view :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`
        """
        # TODO: Remove deprecated arg
        if isinstance(buffer_index, int):
            self._logger.warning("Passing integer as 'buffer_index' to 'set_power_measurement()' is deprecated. One should pass "
                ":class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex` as 'buffer_index' instead.")
            buffer_index = _get_buffer_index_enum_member(buffer_index)
        with ExceptionWrapper():
            return self._device.set_power_measurement(buffer_index, dvm, measurement_type)

    def get_power_measurement(self, buffer_index=MeasurementBufferIndex.MEASUREMENT_BUFFER_INDEX_0, should_clear=True):
        """Read measured power from a long power measurement

        Args:
            buffer_index (:class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex`): Index of the buffer on the firmware the data would be saved at.
                Default is :class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex.MEASUREMENT_BUFFER_INDEX_0`
            should_clear (bool): Flag indicating if the results saved at the firmware will be deleted after reading.

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementData`:
             Object containing measurement data \n
            For :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`: \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.SHUNT_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.BUS_VOLTAGE`: Unit is mV. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.POWER`: Unit is W. \n
            - :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes.CURRENT`: Unit is mA. \n

        Note:
            This function can perform measurements for more than just power.
            For all supported measurement types view
            :class:`~hailo_platform.pyhailort.pyhailort.PowerMeasurementTypes`.
        """
        if self.device_id.device_architecture != DeviceArchitectureTypes.HAILO8_B0:
            raise ControlObjectException("Invalid device architecture: {}".format(self.device_id.device_architecture))
        # TODO: Remove deprecated arg
        if isinstance(buffer_index, int):
            self._logger.warning("Passing integer as 'buffer_index' to 'get_power_measurement()' is deprecated. One should pass "
                ":class:`~hailo_platform.pyhailort.pyhailort.MeasurementBufferIndex` as 'buffer_index' instead.")
            buffer_index = _get_buffer_index_enum_member(buffer_index)
        with ExceptionWrapper():
            return self._device.get_power_measurement(buffer_index, should_clear)

    def _examine_user_config(self):
        with ExceptionWrapper():
            return self._device.examine_user_config()
    
    def read_user_config(self):
        """Read the user configuration section as binary data.

        Returns:
            str: User config as a binary buffer.
        """
        with ExceptionWrapper():
            return self._device.read_user_config()

    def write_user_config(self, configuration):
        """Write the user configuration.

        Args:
            configuration (str): A binary representation of a Hailo device configuration.
        """
        with ExceptionWrapper():
            return self._device.write_user_config(configuration)
    
    def _erase_user_config(self):
        with ExceptionWrapper():
            return self._device.erase_user_config()
    
    def read_board_config(self):
        """Read the board configuration section as binary data.

        Returns:
            str: Board config as a binary buffer.
        """
        with ExceptionWrapper():
            return self._device.read_board_config()

    def write_board_config(self, configuration):
        """Write the static confuration.

        Args:
            configuration (str): A binary representation of a Hailo device configuration.
        """
        with ExceptionWrapper():
            return self._device.write_board_config(configuration)

    def identify(self):
        """Gets the Hailo chip identification.

        Returns:
            class HailoIdentifyResponse with Protocol version.
        """
        with ExceptionWrapper():
            response =  self._device.identify()
        board_information = BoardInformation(response.protocol_version, response.fw_version.major,
            response.fw_version.minor, response.fw_version.revision, response.logger_version,
            response.board_name, response.is_release,  int(response.device_architecture), response.serial_number,
            response.part_number, response.product_name)
        return board_information

    def core_identify(self):
        """Gets the Core Hailo chip identification.

        Returns:
            class HailoIdentifyResponse with Protocol version.
        """
        with ExceptionWrapper():
            response =  self._device.core_identify()
        core_information = CoreInformation(response.fw_version.major, response.fw_version.minor, 
            response.fw_version.revision, response.is_release)
        return core_information

    def set_fw_logger(self, level, interface_mask):
        """Configure logger level and interface of sending.

        Args:
            level (FwLoggerLevel):    The minimum logger level.
            interface_mask (int):     Output interfaces (mix of FwLoggerInterface).
        """
        with ExceptionWrapper():
            return self._device.set_fw_logger(level, interface_mask)

    def set_throttling_state(self, should_activate):
        """Change throttling state of temperature protection component.

        Args:
            should_activate (bool):   Should be true to enable or false to disable. 
        """
        with ExceptionWrapper():
            return self._device.set_throttling_state(should_activate)

    def get_throttling_state(self):
        """Get the current throttling state of temperature protection component.
        
        Returns:
            bool: true if temperature throttling is enabled, false otherwise.
        """
        with ExceptionWrapper():
            return self._device.get_throttling_state()

    def _set_overcurrent_state(self, should_activate):
        """Control whether the overcurrent protection is enabled or disabled.

        Args:
            should_activate (bool):   Should be true to enable or false to disable. 
        """
        with ExceptionWrapper():
            return self._device._set_overcurrent_state(should_activate)

    def _get_overcurrent_state(self):
        """Get the overcurrent protection state.
        
        Returns:
            bool: true if overcurrent protection is enabled, false otherwise.
        """
        with ExceptionWrapper():
            return self._device._get_overcurrent_state()

    @staticmethod
    def _create_c_i2c_slave(pythonic_slave):
        c_slave = _pyhailort.I2CSlaveConfig()
        c_slave.endianness = pythonic_slave.endianness
        c_slave.slave_address = pythonic_slave.slave_address
        c_slave.register_address_size = pythonic_slave.register_address_size
        c_slave.bus_index = pythonic_slave.bus_index
        return c_slave

    def i2c_write(self, slave, register_address, data):
        """Write data to an I2C slave.

        Args:
            slave (:class:`hailo_platform.pyhailort.i2c_slaves.I2CSlave`): I2C slave
                configuration.
            register_address (int): The address of the register to which the data will be written.
            data (str): The data that will be written.
        """
        c_slave = HcpControl._create_c_i2c_slave(slave)
        with ExceptionWrapper():
            return self._device.i2c_write(c_slave, register_address, data, len(data))
        
    def i2c_read(self, slave, register_address, data_length):
        """Read data from an I2C slave.

        Args:
            slave (:class:`hailo_platform.pyhailort.i2c_slaves.I2CSlave`): I2C slave
                configuration.
            register_address (int): The address of the register from which the data will be read.
            data_length (int): The number of bytes to read.

        Returns:
            str: Data read from the I2C slave.
        """
        c_slave = HcpControl._create_c_i2c_slave(slave)
        with ExceptionWrapper():
            return self._device.i2c_read(c_slave, register_address, data_length)
        
    def read_register(self, address):
        """Read the value of a register from a given address.

        Args:
            address (int): Address to read register from.

        Returns:
            int: Value of the register
        """
        register_value, = struct.unpack('!I', self.read_memory(address, type(self).WORD_SIZE))
        return register_value

    def set_bit(self, address, bit_index):
        """Set (turn on) a specific bit at a register from a given address.

        Args:
            address (int) : Address of the register to modify.
            bit_index (int) : Index of the bit that would be set.
        """
        register_value = self.read_register(address)
        register_value |= 1 << bit_index
        self.write_memory(address, struct.pack('!I', register_value))

    def reset_bit(self, address, bit_index):
        """Reset (turn off) a specific bit at a register from a given address.

        Args:
            address (int) :  Address of the register to modify.
            bit_index (int) : Index of the bit that would be reset.
        """
        register_value = self.read_register(address)
        register_value &= ~(1 << bit_index)
        self.write_memory(address, struct.pack('!I', register_value))
    
    def firmware_update(self, firmware_binary, should_reset=True):
        """Update firmware binary on the flash. 
        
        Args:
            firmware_binary (bytes): firmware binary stream.
            should_reset (bool): Should a reset be performed after the update (to load the new firmware)
        """
        with ExceptionWrapper():
            return self._device.firmware_update(firmware_binary, len(firmware_binary), should_reset)

    def second_stage_update(self, second_stage_binary):
        """Update second stage binary on the flash
        
        Args:
            second_stage_binary (bytes): second stage binary stream.
        """
        with ExceptionWrapper():
            return self._device.second_stage_update(second_stage_binary, len(second_stage_binary))

    def store_sensor_config(self, section_index, reset_data_size, sensor_type, config_file_path,
                            config_height=0, config_width=0, config_fps=0, config_name=None):
            
        """Store sensor configuration to Hailo chip flash memory.
        
        Args:
            section_index (int): Flash section index to write to. [0-6]
            reset_data_size (int): Size of reset configuration.
            sensor_type (:class:`~hailo_platform.pyhailort.pyhailort.SensorConfigTypes`): Sensor type.
            config_file_path (str): Sensor configuration file path.
            config_height (int): Configuration resolution height.
            config_width (int): Configuration resolution width.
            config_fps (int): Configuration FPS.
            config_name (str): Sensor configuration name.
        """
        if config_name is None:
            config_name = "UNINITIALIZED"

        with ExceptionWrapper():
            return self._device.sensor_store_config(section_index, reset_data_size, sensor_type, config_file_path,
            config_height, config_width, config_fps, config_name)
    
    def store_isp_config(self, reset_config_size, isp_static_config_file_path, isp_runtime_config_file_path,
                         config_height=0, config_width=0, config_fps=0, config_name=None):
        """Store sensor isp configuration to Hailo chip flash memory.

        Args:
            reset_config_size (int): Size of reset configuration.
            isp_static_config_file_path (str): Sensor isp static configuration file path.
            isp_runtime_config_file_path (str): Sensor isp runtime configuration file path.
            config_height (int): Configuration resolution height.
            config_width (int): Configuration resolution width.
            config_fps (int): Configuration FPS.
            config_name (str): Sensor configuration name.
        """
        if config_name is None:
            config_name = "UNINITIALIZED"

        with ExceptionWrapper():
            return self._device.store_isp_config(reset_config_size, config_height, config_width, 
            config_fps, isp_static_config_file_path, isp_runtime_config_file_path, config_name)

    def get_sensor_sections_info(self):
        """Get sensor sections info from Hailo chip flash memory.

        Returns:
            Sensor sections info read from the chip flash memory.
        """
        with ExceptionWrapper():
            return self._device.sensor_get_sections_info()
    
    def sensor_set_generic_i2c_slave(self, slave_address, register_address_size, bus_index, should_hold_bus, endianness):
        """Set a generic I2C slave for sensor usage.

        Args:
            sequence (int): Request/response sequence.
            slave_address (int): The address of the I2C slave.
            register_address_size (int): The size of the offset (in bytes).
            bus_index (int): The number of the bus the I2C slave is behind.
            should_hold_bus (bool): Hold the bus during the read.
            endianness (:class:`~hailo_platform.pyhailort.pyhailort.Endianness`):
                Big or little endian.
        """
        with ExceptionWrapper():
            return self._device.sensor_set_generic_i2c_slave(slave_address, register_address_size, bus_index, should_hold_bus, endianness)

    def set_sensor_i2c_bus_index(self, sensor_type, i2c_bus_index):
        """Set the I2C bus to which the sensor of the specified type is connected.
  
        Args:
            sensor_type (:class:`~hailo_platform.pyhailort.pyhailort.SensorConfigTypes`): The sensor type.
            i2c_bus_index (int): The I2C bus index of the sensor.
        """
        with ExceptionWrapper():
            return self._device.sensor_set_i2c_bus_index(sensor_type, i2c_bus_index)

    def load_and_start_sensor(self, section_index):
        """Load the configuration with I2C in the section index.
  
        Args:
            section_index (int): Flash section index to load config from. [0-6]
        """
        with ExceptionWrapper():
            return self._device.sensor_load_and_start_config(section_index)

    def reset_sensor(self, section_index):
        """Reset the sensor that is related to the section index config.

        Args:
            section_index (int): Flash section index to reset. [0-6]
        """
        with ExceptionWrapper():
            return self._device.sensor_reset(section_index)

    def wd_enable(self, cpu_id):
        """Enable firmware watchdog.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
        """
        with ExceptionWrapper():
            return self._device.wd_enable(cpu_id)

    def wd_disable(self, cpu_id):
        """Disable firmware watchdog.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
        """
        with ExceptionWrapper():
            return self._device.wd_disable(cpu_id)

    def wd_config(self, cpu_id, wd_cycles, wd_mode):
        """Configure a firmware watchdog.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
            wd_cycles (int): number of cycles until watchdog is triggered.
            wd_mode (int): 0 - HW/SW mode, 1 -  HW only mode
        """
        with ExceptionWrapper():
            return self._device.wd_config(cpu_id, wd_cycles, wd_mode)

    def previous_system_state(self, cpu_id):
        """Read the FW previous system state.

        Args:
            cpu_id (:class:`~hailo_platform.pyhailort.pyhailort.HailoCpuId`): 0 for App CPU, 1 for Core CPU.
        """
        with ExceptionWrapper():
            return self._device.previous_system_state(cpu_id)

    def get_chip_temperature(self):
        """Returns the latest temperature measurements from the 2 internal temperature sensors of the Hailo chip.

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.TemperatureInfo`:
             Temperature in celsius of the 2 internal temperature sensors (TS), and a sample
             count (a running 16-bit counter)
        """
        with ExceptionWrapper():
            return self._device.get_chip_temperature()

    def get_extended_device_information(self):
        with ExceptionWrapper():
            response = self._device.get_extended_device_information()
        device_information = ExtendedDeviceInformation(response.neural_network_core_clock_rate,
            response.supported_features, response.boot_source, response.lcs, response.soc_id,  response.eth_mac_address , response.unit_level_tracking_id, response.soc_pm_values)
        return device_information

    def _get_health_information(self):
        with ExceptionWrapper():
            response = self._device._get_health_information()
        health_information = HealthInformation(response.overcurrent_protection_active, response.current_overcurrent_zone, response.red_overcurrent_threshold,
                    response.orange_overcurrent_threshold, response.temperature_throttling_active, response.current_temperature_zone, response.current_temperature_throttling_level, 
                    response.temperature_throttling_levels, response.orange_temperature_threshold, response.orange_hysteresis_temperature_threshold,
                    response.red_temperature_threshold, response.red_hysteresis_temperature_threshold)
        return health_information

    def set_pause_frames(self, rx_pause_frames_enable):
        """Enable/Disable Pause frames.

        Args:
            rx_pause_frames_enable (bool): False for disable, True for enable.
        """
        with ExceptionWrapper():
            return self._device.set_pause_frames(rx_pause_frames_enable)

    def test_chip_memories(self):
        """test all chip memories using smart BIST

        """
        with ExceptionWrapper():
            return self._device.test_chip_memories()

    def _get_device_handle(self):
        return self._device

class UdpHcpControl(HcpControl):
    """Control object that uses a HCP over UDP controller interface."""

    def __init__(self, remote_ip, device=None, remote_control_port=22401, retries=2, response_timeout_seconds=10.0, ignore_socket_errors=False):
        """Initializes a new UdpControllerControl object.

        Args:
            remote_ip (str): The IPv4 address of the remote Hailo device (X.X.X.X).
            remote_control_port (int, optional): The port that the remote Hailo device listens on.
            response_timeout_seconds (float, optional): Number of seconds to wait until a response is received.
            ignore_socket_errors (bool, optional): Ignore socket error (might be usefull for debugging).
        """
        super(UdpHcpControl, self).__init__()

        # In the C API we define the total amount of attempts, instead of the amount of retries.
        max_number_of_attempts = retries + 1
        response_timeout_milliseconds = int(response_timeout_seconds * 1000)
        if device is None:
            with ExceptionWrapper():
                self.device = _pyhailort.Device.create_eth(remote_ip, remote_control_port,
                    response_timeout_milliseconds, max_number_of_attempts)
        else:
            self._device = device.device
        self._device_id = self.identify()


class PcieHcpControl(HcpControl):
    """Control object that uses a HCP over PCIe controller interface."""

    def __init__(self, device=None, device_info=None):
        """Initializes a new HailoPcieController object."""
        super(PcieHcpControl, self).__init__()

        if device_info is None:
            device_info = InternalPcieDevice.scan_devices()[0]

        if device is None:
            with ExceptionWrapper():
                self._device = _pyhailort.Device.create_pcie(device_info)
        else:
            self._device = device.device
        self._device_id = self.identify()
    
    def set_notification_callback(self, callback_func, notification_id, opaque):
        """Set a callback function to be called when a notification is received.

        Args:
            callback_func (function): Callback function with the parameters (device, notification, opaque).
                Note that throwing exceptions is not supported and will cause the program to terminate with an error!
            notification_id (NotificationId): Notification ID to register the callback to.
            opauqe (object): User defined data.

        Note:
            The notifications thread is started and closed in the use_device() context, so
            notifications can only be received there.
        """
        with ExceptionWrapper():
            return self._device.set_notification_callback(callback_func, notification_id, opaque)

    def remove_notification_callback(self, notification_id):
        """Remove a notification callback which was already set.

        Args:
            notification_id (NotificationId): Notification ID to remove the callback from.
        """
        with ExceptionWrapper():
            return self._device.remove_notification_callback(notification_id)
