#!/usr/bin/env python

"""Parses a configuration file that contains the definitions of possible user configurations
    Availabe in the firmware.
"""

from builtins import object
import json

from abc import ABCMeta
from collections import OrderedDict
from future.utils import with_metaclass



class ConfigSyntaxException(Exception):
    """Raised when there is a syntax error in the configuration file."""
    pass


class UnsupportedConfigVersionException(Exception):
    """Raised when the version of the configuration file is not supported."""
    pass


class ConfigFormattingException(Exception):
    """Raised when there is a formatting error in the firmware's configuration."""
    pass

class ConfigDeserializationException(Exception):
    """Raised when there is an error while parsing a binary configuration."""
    pass


class ConfigLine(with_metaclass(ABCMeta, object)):
    """An abstract config line class. Implements logic that is shared between a few types
        of configuration lines.
    """

    def __init__(self, config_name, config_id):
        """Initializes a new ConfigLine object.

        Args:
            config_name(str): Name defined for the configuration in the line.
            config_id(int): Config ID (the integer representing the config).
        """
        self._name = None
        self.name = config_name
        self._id = config_id

    @property
    def name(self):
        """Returns:
            str: Name of the category.
        """
        return self._name

    @name.setter
    def name(self, config_name):
        if not (config_name.isalnum() or config_name[0].isalpha()):
            raise ConfigSyntaxException('Configuration name must be alphanumeric (with a first '
                                        'alphabetical character) received: {}'.format(
                                            config_name))
        self._name = config_name.lower()

    @property
    def id_value(self):
        """Returns:
            int: Value of the entry's ID.
        """
        return self._id


class ConfigEntry(ConfigLine):
    """A configuration entry."""

    _PRIMITIVE_TYPES = {
        1: 'int8_t',
        2: 'int16_t',
        4: 'int32_t'}

    def __init__(self, entry_name, entry_id, category_id, size, length, sign, deserializer, length_before_serialization=None):
        """Initializes a new ConfigEntry object.

        Args:
            entry_name(str): Entry name.
            entry_id(int): Entry ID (the integer representing the entry). (Should be 0-65535)
            category_id(int): ID of the category that contains this entry. (Should be 0-65535)
            size(int): Size of the entry in bytes. Only primitive sizes are supported.
            length(int): Length of the entry, defined as an array if different than 0.
            sign(bool): Is the primitive signed.
            deserializer(str): The formatter to use when deserializing the entry.
            length_before_serialization(int): Length of the entry before serialization, should be given only if this length
                                                is different than size
        """
        super(ConfigEntry, self).__init__(entry_name, entry_id)
        self._category_id = category_id
        if size not in self._PRIMITIVE_TYPES:
            raise ConfigSyntaxException(
                'Unsupported entry size. Supported sizes {}.'.format(list(self._PRIMITIVE_TYPES.keys())))
        self._size = size
        self._length = length
        self._sign = sign
        self._deserializer = deserializer
        if length_before_serialization is None:
            self._length_before_serialization = size
        else:
            self._length_before_serialization = length_before_serialization

    @property
    def deserializer(self):
        """Returns:
            str: The formatter to use when deserializing the entry.
        """
        return self._deserializer

    @property
    def primitive_type(self):
        """Returns:
            str: The string representing the primitve C type of the entry.
        """
        var_type = self._PRIMITIVE_TYPES[self._size]
        if not self._sign:
            var_type = 'u{}'.format(var_type)
        return var_type

    @property
    def category_id(self):
        """Returns:
            int: Entry id value.
        """
        return self._category_id

    @property
    def length(self):
        """Returns:
            int: Length of the entry, if this is different than zero, then the entry is an array.
        """
        return self._length

    @property
    def total_size(self):
        """Returns:
            int: Entry's total size in bytes.
        """
        if self._length == 0:
            return self._size
        else:
            return self._length * self._size

    @property
    def length_before_serialization(self):
        """Returns:
            int: The length of the entry before serialization
        """
        return self._length_before_serialization

class ConfigCategory(ConfigLine):
    """A configuration category that contains multiple configuration IDs"""

    def __init__(self, category_name, category_id):
        """Initializes a new ConfigCategory object.

        Args:
            category_name(str): Category name.
            category_id(int): Category ID (the integer representing the category).
        """
        super(ConfigCategory, self).__init__(category_name, category_id)
        self._config_entries = OrderedDict()
        self._current_entry_id = 0

    def append(self, entry_name, size, deserialize_as, length=0, sign=False, length_before_serialization=None):
        """Adds a new entry to the category

        Args:
            entry_name(str): Name of the appended configuration entry.
            size(int): Size of the entry in bytes. Only primitive sizes are supported.
            length(int): Length of the entry, defined as an array if different than 0.
            sign(bool): Is the primitive signed.
            deserialize_as(str): The formatter to use when deserializing the entry.
        """
        config_entry = ConfigEntry(
            entry_name,
            self._current_entry_id,
            self._id,
            size,
            length,
            sign,
            deserialize_as,
            length_before_serialization)
        self._config_entries[config_entry.name] = config_entry
        self._current_entry_id += 1

    @property
    def entries(self):
        """Returns:
            list of :obj:`ConfigEntry`: The entries in this category object.
        """
        return self._config_entries

    def __getitem__(self, key):
        """Returns:
            int: The ID value of the requested entry key.
        """
        return self._config_entries[key.lower()]


class FirmwareConfig(object):
    """This class wraps the configuration file that defines the firmware user config."""
    _SUPPORTED_VERSION = [0]
    _MAGIC_NUMBER = 0x1FF6A40B

    def __init__(self, config_path):
        """Initializes a new FirmwareConfig object.

        Args:
            config_path(str): Path to a configuration file.
        """
        config_file = open(config_path, 'r')
        config_json = json.load(config_file, object_pairs_hook=OrderedDict)
        self._current_category = None
        self._next_category_id = 0
        self._categories = OrderedDict()
        self._parse_config_json(config_json)

    def _parse_config_json(self, config_json):
        """Parses a json dictionary containing the configuration and assigns it to the
        object attributes.

        Args:
            config_json(dict of str): A dictionary containing the configration file's content.
        """
        try:
            version = config_json['version']
        except KeyError:
            raise ConfigSyntaxException('Error: Version definition not found.')
        if version not in self._SUPPORTED_VERSION:
            raise UnsupportedConfigVersionException('Unsupported version: {}.\n'
                                                    'Supported versions: {}'.format(
                                                        version, self._SUPPORTED_VERSION))
        self.version = version

        try:
            categories = config_json['categories']
        except KeyError:
            raise ConfigSyntaxException('Error: Categories definition not found.')

        for i, category in enumerate(categories):
            category_object = ConfigCategory(category, i)
            try:
                entries = config_json['categories'][category]['entries']
            except KeyError:
                raise ConfigSyntaxException('Error: Category {} does not contain entries.'.format(
                    category))
            for entry in entries:
                category_object.append(entry, **entries[entry])
            self._categories[category_object.name] = category_object

    @property
    def supported_version(self):
        """Returns:
            list of int: A list containing the supported configuration version.
        """
        return self._SUPPORTED_VERSION

    @property
    def magic(self):
        """Returns:
            int: Firmware configuration magic number value.
        """
        return self._MAGIC_NUMBER

    @property
    def categories(self):
        """Returns:
            list of :obj:`ConfigCategory`: List of configuration categories contained in the
                firmware configuration.
        """
        return self._categories

    def __getitem__(self, key):
        """Returns:
            :class:`sdk_client.hailo_sdk_client.tools.firmware.ConfigCategory: The category
            corresponding to the requested key.
        """
        return self._categories[key]