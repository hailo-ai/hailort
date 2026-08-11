from hailo_platform import _verify_version

_verify_version()

from hailo_platform import VDevice
from hailo_platform.pyhailort.pyhailort import LLM, VLM, Speech2Text, Speech2TextTask

__all__ = ['VDevice', 'LLM', 'VLM', 'Speech2Text', 'Speech2TextTask']