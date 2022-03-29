import os
import sys
from setuptools import setup, find_packages
from wheel.bdist_wheel import bdist_wheel as orig_bdist_wheel


class NonPurePythonBDistWheel(orig_bdist_wheel):
    """makes the wheel platform-dependant so it can be based on the _pyhailort architecture"""

    def finalize_options(self):
        orig_bdist_wheel.finalize_options(self)
        self.root_is_pure = False


def _get_pyhailort_lib():
    extension = {
        "posix": "so",
        "nt": "pyd",  # Windows
    }[os.name]
    py = "".join(map(str, sys.version_info[:2]))

    return f"drivers/hailort/_pyhailort*{py}*.{extension}"


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
                "hailo=hailo_platform.tools.cmd_utils.main:main",
            ]
        },
        install_requires=[
            "argcomplete",
            "contextlib2",
            "future",
            "netaddr",
            "netifaces",
            "six",
            "verboselogs",
            # Pinned versions
            "numpy==1.19.4",
        ],
        name="hailort",
        package_data={
            "hailo_platform": [
                _get_pyhailort_lib(),  # packs _pyhailort library for _pyhailort imports
            ],
        },
        packages=find_packages(),
        platforms=[
            "linux_x86_64",
            "linux_aarch64",
        ],
        url="https://hailo.ai/",
        version="4.6.0",
        zip_safe=False,
    )
