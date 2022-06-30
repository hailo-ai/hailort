#!/usr/bin/env python
from builtins import object
import struct

from hailo_platform.common.logger.logger import default_logger
from hailo_platform.pyhailort.pyhailort import Endianness
logger = default_logger()

#: Variable which defines that the I2C slave is not behind a switch.
NO_I2C_SWITCH = 5

class I2CSlavesException(Exception):
    pass


class I2CSlave(object):
    def __init__(self, name, bus_index, slave_address, switch_number=NO_I2C_SWITCH,
                 register_address_size=1, endianness=Endianness.LITTLE_ENDIAN,
                 should_hold_bus=False):
        """Initialize a class which describes an I2C slave.

        Args:
            name (str): The name of the I2C slave.
            bus_index (int): The bus number the I2C slave is connected to.
            slave_address (int): The address of the I2C slave.
            switch_number (int): The number of the switch the i2c salve is connected to.
            register_address_size (int): Slave register address length (in bytes).
            endianness (:class:`~hailo_platform.pyhailort.pyhailort.Endianness`): The endianness of the slave.
            should_hold_bus (bool): Should hold the bus during the read.

        """
        self._name = name
        self._bus_index = bus_index
        self._slave_address = slave_address
        self._switch_number = switch_number
        self._register_address_size = register_address_size
        self._endianness = endianness
        self._should_hold_bus = should_hold_bus

    def __repr__(self):
        # Returning '' for the sphinx doc
        return ''

    @property
    def name(self):
        """Get the name of the I2C slave.

        Returns:
            str: Name of the I2C slave.
        """
        return self._name

    @property
    def bus_index(self):
        """Get bus index the I2C slave is connected to.

        Returns:
            int: Index of the bus the I2C slave is connected to.
        """
        return self._bus_index

    @property
    def slave_address(self):
        """Get the address of the salve.

        Returns:
            int: The address of the I2C slave.
        """
        return self._slave_address

    @property
    def register_address_size(self):
        """Get the slave register address length (in bytes). This number represents how many bytes are in the
        register address the slave can access.

        Returns:
            int: Slave register address length.

        Note:
            Pay attention to the slave endianness (:class:`~hailo_platform.pyhailort.pyhailort.Endianness`).
        """
        return self._register_address_size

    @property
    def switch_number(self):
        """Get the switch number the slave is connected to.

        Returns:
            int: The number of the switch the I2C is behind.

        Note:
            If :data:`NO_I2C_SWITCH` is returned, it means the slave is not behind a switch.
        """
        return self._switch_number

    @property
    def endianness(self):
        """Get the slave endianness.

        Returns:
            :class:`~hailo_platform.pyhailort.pyhailort.Endianness`: The slave endianness.
        """
        return self._endianness

    @property
    def should_hold_bus(self):
        """Returns a Boolean indicating if the bus will be held while reading from the slave.

        Returns:
            bool: True if the bus would be held, otherwise False.
        """
        return self._should_hold_bus

# DVM's
#: Class which represents the MIPI AVDD I2C slave.
I2C_SLAVE_MIPI_AVDD = I2CSlave("DVM_MIPI_AVDD", 0, 0x40)
#: Class which represents the USB AVDD IO slave.
I2C_SLAVE_USB_AVDD_IO = I2CSlave("DVM_USB_AVDD_IO", 0, 0x41)
#: Class which represents the V_CORE slave.
I2C_SLAVE_VDD_CORE = I2CSlave("DVM_VDD_CORE", 0, 0x42)
#: Class which represents the VDD TOP slave.
I2C_SLAVE_VDD_TOP = I2CSlave("DVM_VDD_TOP", 0, 0x43)
#: Class which represents the MIPI AVDD_H I2C slave.
I2C_SLAVE_MIPI_AVDD_H = I2CSlave("DVM_MIPI_AVDD_H", 0, 0x44)
#: Class which represents the DVM USB AVDD IO HV slave.
I2C_SLAVE_USB_AVDD_IO_HV = I2CSlave("DVM_USB_AVDD_IO_HV", 0, 0x45)
#: Class which represents the DVM_VDDIO slave.
I2C_SLAVE_VDD_IO = I2CSlave("DVM_VDD_IO", 0, 0x46)
#: Class which represents the DVM_AVDD_H slave.
I2C_SLAVE_AVDD_H = I2CSlave("DVM_AVDD_H", 0, 0x47)
#: Class which represents the DVM_SDIO_VDDIO slave.
I2C_SLAVE_SDIO_VDD_IO = I2CSlave("DVM_SDIO_VDD_IO", 0, 0x4d)

#: Class which represents the DVM_SDIO_VDDIO slave.
I2C_SLAVE_M_DOT_2_OVERCURREN_PROTECTION = I2CSlave("M_DOT_2_OVERCURREN_PROTECTION", 0, 0x40)

#: Class which represents the I2S codec I2C slave.
I2C_SLAVE_I2S_CODEC = I2CSlave("I2S_codec", 1, 0x18, should_hold_bus=True)

#: Class which represents the I2C to gpio I2C slave.
I2C_SLAVE_I2C_TO_GPIO = I2CSlave("I2C_to_GPIO", 0, 0x22)
#: Class which represents the I2C switch slave.
I2C_SLAVE_SWITCH = I2CSlave("I2C_SWITCH", 1, 0x70)

#: Class which represents the I2C TEMP_sensor_0 slave.
I2C_SLAVE_TEMP_SENSOR_0 = I2CSlave("TEMP_sensor_0", 0, 0x29)
#: Class which represents the I2S TEMP_sensor_1 slave.
I2C_SLAVE_TEMP_SENSOR_1 = I2CSlave("TEMP_sensor_1", 0, 0x2A)

#: Class which represents the EEPROM I2C slave.
I2C_SLAVE_EEPROM = I2CSlave("EEPROM", 0, 0x50, register_address_size=2,
                              endianness=Endianness.BIG_ENDIAN)

# External hardware
#: Class which represents the raspicam I2C slave.
I2C_SLAVE_RASPICAM = I2CSlave("RaspiCam", 1, 0x36, switch_number=1, register_address_size=2,
                              endianness=Endianness.BIG_ENDIAN)

I2C_SLAVE_ONSEMI_CAMERA_AR0220 = I2CSlave('Onsemi', 1, 0x10, switch_number=0, register_address_size=2,
                              endianness=Endianness.BIG_ENDIAN)

I2C_SLAVE_ONSEMI_CAMERA_AS0149 = I2CSlave('Onsemi', 1, (0x90 >> 1), switch_number=0, register_address_size=2,
                              endianness=Endianness.BIG_ENDIAN)

def set_i2c_switch(control_object, slave, slave_switch=None):
    """Set the I2C switch in order to perform actions from the I2C slave.

    Args:
        control_object (:class:`~hailo_platform.pyhailort.control_object.HcpControl`): Control object
            which communicates with the Hailo chip.
        slave (:class:`I2CSlave`): Slave which the switch is set for.
        slave_switch (:class:`I2CSlave`): The I2C slave for the switch it self. Defaults to
            :data:`I2C_SLAVE_SWITCH`.
    """
    I2C_SWITCH_REGISTER_SIZE = 1
    if NO_I2C_SWITCH != slave.switch_number:
        if not slave_switch:
            slave_switch = I2C_SLAVE_SWITCH

        # Set the switch value that should be written
        switch_value = 1 << slave.switch_number

        # Write new value to the switch
        control_object.i2c_write(slave_switch, switch_value, struct.pack('b', switch_value))

        # Read data from the switch, make sure write was successful
        read_data, = struct.unpack('b', control_object.i2c_read(slave_switch, switch_value, I2C_SWITCH_REGISTER_SIZE))
        if read_data != switch_value:
            raise I2CSlavesException("Switch writing has failed. Read data is different then expected %s != %s" % (
                read_data,
                switch_value))
