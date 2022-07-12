import os
import json
from setuptools import setup, find_packages
from wheel.bdist_wheel import bdist_wheel as orig_bdist_wheel


class NonPurePythonBDistWheel(orig_bdist_wheel):
    """makes the wheel platform-dependant so it can be based on the _pyhailort architecture"""

    def finalize_options(self):
        orig_bdist_wheel.finalize_options(self)
        self.root_is_pure = False


def _get_pyhailort_lib_path():
    conf_file_path = os.path.join(os.path.abspath(os.path.dirname( __file__ )), "wheel_conf.json")
    extension = {
        "posix": "so",
        "nt": "pyd",  # Windows
    }[os.name]
    if not os.path.isfile(conf_file_path):
        return None

    with open(conf_file_path, "r") as conf_file:
        content = json.load(conf_file)
        return f"../hailo_platform/pyhailort/_pyhailort*{content['py_version']}*{content['arch']}*.{extension}"

def _get_package_paths():
    packages = []
    pyhailort_lib = _get_pyhailort_lib_path()
    if pyhailort_lib: 
        packages.append(pyhailort_lib)
    packages.append("../hailo_tutorials/notebooks/*")
    packages.append("../hailo_tutorials/hefs/*")
    return packages


if __name__ == "__main__":
    setup(
        author="Hailo team",
        author_email="contact@hailo.ai",
        cmdclass={
            "bdist_wheel": NonPurePythonBDistWheel,
        },
        description="HailoRT",
        entry_points={
            "console_scripts": [
                "hailo=hailo_platform.tools.hailocli.main:main",
            ]
        },
        install_requires=[
            "argcomplete",
            "contextlib2",
            "future",
            "netaddr",
            "netifaces",
            "verboselogs",
            # Pinned versions
            "numpy==1.19.4",
        ],
        name="hailort",
        package_data={
            "hailo_platform": _get_package_paths(),
        },
        packages=find_packages(),
        platforms=[
            "linux_x86_64",
            "linux_aarch64",
        ],
        url="https://hailo.ai/",
        version="4.8.1",
        zip_safe=False,
    )
