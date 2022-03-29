#!/usr/bin/env python
import os
from configparser import ConfigParser
from hailo_platform.common.paths_manager.paths import PackingInfo, PackingStatus, SDKPaths
from hailo_platform.paths_manager.paths import PlatformPaths


class ConfigFileNotFoundException(Exception):
    pass


def get_home_hailo_dir():
    return os.path.expanduser('~/.hailo')

def get_parsed_config_from_path(config_path=None):
    actual_config_path = _get_config_path_with_default(config_path)
    config = ConfigParser()
    with open(actual_config_path, 'r') as config_file:
        config.read_file(config_file)
    return config


def _get_config_path_with_default(config_path=None):
    if config_path is not None and os.path.isfile(config_path):
        return config_path
    default_path = _get_config_path()
    if os.path.isfile(default_path):
        return default_path
    raise ConfigFileNotFoundException('Could not find configuration file at default path: {}.'.format(default_path))


def _get_config_path():
    config_file_name = 'config'

    if PackingInfo().status in [PackingStatus.unpacked]:
        full_path = os.path.join(SDKPaths().join_sdk('../'), config_file_name)
        if os.path.exists(full_path):
            return full_path
        #This is a CI nightly workaround because we are in unpack mode but installing whl's 
        #In this case SDKPaths() is inside the site packages and not the sdk root. the workaround is to look for local dir
        elif os.path.exists(config_file_name):
            return config_file_name

    elif PackingInfo().status in [PackingStatus.standalone_platform]:
        full_path = os.path.join(PlatformPaths().join_platform('../../'), config_file_name)
        if os.path.exists(full_path) and os.path.isfile(full_path):
            return full_path

    elif os.path.exists(config_file_name):
        return config_file_name

    return os.path.join(get_home_hailo_dir(), config_file_name)
