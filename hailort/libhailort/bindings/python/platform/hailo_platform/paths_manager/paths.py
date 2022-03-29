#!/usr/bin/env python
import hailo_platform
from hailo_platform.common.paths_manager.paths import SDKPaths, Singleton
import os
from future.utils import with_metaclass


class PlatformPaths(with_metaclass(Singleton, SDKPaths)):
    def join_platform(self, path):
        return os.path.join(os.path.abspath(hailo_platform.__path__[0]), path)
