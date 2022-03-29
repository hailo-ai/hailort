#!/usr/bin/env python
from builtins import object
import os
import shutil
import copy
from enum import Enum


from hailo_platform.common.logger.logger import default_logger
from future.utils import with_metaclass

logger = default_logger()


class ConfigStageNotSetException(Exception):
    pass

class PackagingException(Exception):
    pass

class Singleton(type):
    _instances = {}

    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super(Singleton, cls).__call__(*args, **kwargs)
        return cls._instances[cls]


class PackingStatus(Enum):
    unpacked = 'unpacked'
    packed_server = 'packed_server'
    packed_client = 'packed_client'
    standalone_platform = 'standalone_platform'


class PackingInfo(with_metaclass(Singleton, object)):
    def __init__(self):
        self._status = PackingStatus.unpacked
        self._has_graphviz = True

    def is_packed(self):
        return self._status not in [PackingStatus.unpacked]

    @property
    def status(self):
        return self._status

    @status.setter
    def status(self, val):
        self._status = val

    @property
    def has_graphviz(self):
        return self._has_graphviz

    @has_graphviz.setter
    def has_graphviz(self, val):
        self._has_graphviz = val


class SDKPaths(with_metaclass(Singleton, object)):
    DEFAULT_BUILD_DIR = 'build'

    def __init__(self):
        self._build_dir_name = type(self).DEFAULT_BUILD_DIR
        self._build_dir_path = '.'
        self._custom_build_dir = None

    @property
    def _sdk_path(self):
        packaging_status = PackingInfo().status
        if packaging_status == PackingStatus.packed_server:
            import hailo_sdk_common
            return os.path.dirname(hailo_sdk_common.__path__[0])
        if packaging_status == PackingStatus.packed_client:
            return ''
        if packaging_status == PackingStatus.standalone_platform:
            raise PackagingException(
                'the packaging status is \'standalone_platform\', and there was a call to a sdk method')
        import hailo_sdk_common
        return os.path.join(os.path.dirname(os.path.dirname(hailo_sdk_common.__path__[0])), 'sdk_server')

    @property
    def custom_build_dir(self):
        return self._custom_build_dir

    @custom_build_dir.setter
    def custom_build_dir(self, custom_build_dir):
        self._custom_build_dir = custom_build_dir

    def join_sdk(self, path):
        return os.path.join(self._sdk_path, path)

    def join_sdk_common(self, path):
        import hailo_sdk_common
        if PackingInfo().status == PackingStatus.packed_server:
            return self.join_sdk(os.path.join('hailo_sdk_common', path))
        return os.path.join(os.path.abspath(hailo_sdk_common.__path__[0]), path)

    def set_client_build_dir_path(self):
        if PackingInfo().status == PackingStatus.unpacked:
            self._build_dir_path = '../sdk_client'

    def set_server_build_dir_path(self):
        if PackingInfo().status == PackingStatus.unpacked:
            self._build_dir_path = '../sdk_server'

    def set_build_dir(self, build_dir_name=None, clean=False):
        self._build_dir_name = build_dir_name if build_dir_name is not None else type(self).DEFAULT_BUILD_DIR
        logger.debug('Build dir name: {}'.format(self._build_dir_name))
        build_dir = self.build_dir
        if os.path.exists(build_dir):
            if clean:
                logger.debug('Deleting build dir : {}'.format(build_dir))
                shutil.rmtree(build_dir)
                self._make_build_dir(build_dir)
            return
        self._make_build_dir(build_dir)

    @property
    def build_dir(self):
        if self._custom_build_dir:
            return self._custom_build_dir
        return os.path.join(self._sdk_path, self._build_dir_path, self._build_dir_name)

    def join_build_sdk(self, path):
        build_dir = self.build_dir
        if os.path.exists(build_dir):
            return os.path.join(build_dir, path)
        logger.debug('Creating build dir : {}'.format(build_dir))
        self._make_build_dir(build_dir)
        return os.path.join(build_dir, path)

    def _make_build_dir(self, build_dir):
        os.makedirs(build_dir)


class BaseConfigDirs(object):

    DIRS_BUILD_ONLY = {
        'outputs_dir': ['outputs'],
        'bin_dir': ['bin'],
        'weights_dir': ['data', 'weights'],
        'inputs_dir': ['data', 'inputs'],
    }

    DIRS_SDK_ONLY = {}

    DIRS_BOTH = {}

    def __init__(self, hw_arch):
        self._hw_arch = hw_arch.name
        self._paths = SDKPaths()
        self._dirs = {}
        for d in [type(self).DIRS_BUILD_ONLY, type(self).DIRS_SDK_ONLY, type(self).DIRS_BOTH]:
            self._dirs.update(self._format_dirs(d))

    def get_dir(self, name, in_build=True):
        return self._join_base(self._dirs[name], in_build)

    def _format_dirs(self, input_dirs):
        result = {}
        for name, dir_path in input_dirs.items():
            result[name] = os.path.join(*dir_path).format(hw_arch=self._hw_arch)
        return result

    def _join_base(self, path, in_build=True):
        base_dir = self._paths.build_dir if in_build else 'microcode'
        whole_path = os.path.join(base_dir, path)
        return self._paths.join_sdk(whole_path)

    @property
    def build_dirs_keys(self):
        return list(type(self).DIRS_BUILD_ONLY.keys()) + list(type(self).DIRS_BOTH.keys())


class BaseConfigPaths(BaseConfigDirs):

    PATHS = {
        'bin': ['{bin_dir}', '{network_name}.mem'],
    }

    def __init__(self, hw_arch, model_name):
        super(BaseConfigPaths, self).__init__(hw_arch)
        self._model_name = model_name
        self._stage = None
        self._stage_only = False

    def set_stage(self, stage, stage_only=False):
        self._stage = stage
        self._stage_only = stage_only

    def get_path(self, path_name, in_build=True):
        template = os.path.join(*type(self).PATHS[path_name])
        if self._stage is None and '{network_name}' in template:
            raise ConfigStageNotSetException('Set the stage before trying to get paths')
        format_dict = copy.deepcopy(self._dirs)
        format_dict['model_name'] = self._model_name
        if self._stage_only:
            format_dict['network_name'] = self._stage
        else:
            format_dict['network_name'] = '{model_name}.{stage}'.format(
                model_name=self._model_name, stage=self._stage
            )
        config_path = template.format(**format_dict)
        return self._join_base(config_path, in_build)
