"""
Builds hailo_platform python package and its C++ dependencies using cmake
"""

import re
import os
import subprocess
import sys
import shutil
import glob

from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext as orig_build_ext
from setuptools.command.install_lib import install_lib as orig_install_lib
from wheel.bdist_wheel import bdist_wheel as orig_bdist_wheel

_build_type = os.environ.get("CMAKE_BUILD_TYPE", "Release")
_plat_name = ""


class install_lib(orig_install_lib):
    def install(self):
        """
        When cross compiling, the extension is not automatically copied into the install dir, and therefore it needs to be done manually.
        """
        outfiles = super().install()

        arch = re.sub(_plat_name, "linux[_-]", "")  # remove linux prefix as the extension architecture does not include it
        extension = "pyd" if os.name == "nt" else "so"
        py_version = f"{sys.version_info.major}{sys.version_info.minor}"
        lib_regex = f"_pyhailort*{py_version}*{arch}*.{extension}"
        dst = os.path.join(self.install_dir, "hailo_platform", "pyhailort")
        already_copied = len(glob.glob(f"{dst}/{lib_regex}")) > 0
        if not already_copied:
            current_dir = Path(__file__).parent.absolute()
            lib = current_dir.rglob(f"*_pyhailort.*{extension}")
            shutil.copy2(str(next(lib)), dst)

        return outfiles


class bdist_wheel(orig_bdist_wheel):
    def finalize_options(self):
        """
        Force the wheel name to include the platform name based on the extension module.
        """
        super().finalize_options()
        self.root_is_pure = False

        global _plat_name
        _plat_name = self.plat_name  # update plat_name to allow access in install_lib


class build_ext(orig_build_ext):
    OPTIONAL_CMAKE_ENV_VARIABLES = [
        "CMAKE_BUILD_TYPE",
        "CMAKE_GENERATOR",
        "CMAKE_TOOLCHAIN_FILE",
        "HAILORT_INCLUDE_DIR",
        "LIBHAILORT_PATH",
        "PYTHON_EXECUTABLE",
        "PYTHON_INCLUDE_DIRS",
        "PYTHON_LIBRARY",
        "PYBIND11_FINDPYTHON",
    ]

    def run(self):
        """
        Defines a cmake command that will be called from the python build process.
        The cmake command will build the C++ extension (_pyhailort) and install it.
        Multiple CMake variables can be passed as environment variables to control the target library.
        """
        build_args = f"--config {_build_type} --target install"
        python_version = f"{sys.version_info.major}.{sys.version_info.minor}"

        current_dir = Path(__file__).parent.absolute()
        cmake_list_dir = current_dir.parent / "src"
        build_dir = current_dir / "build"

        cmake_args = [
            f"-B{build_dir}",
            f"-DCMAKE_BUILD_TYPE={_build_type}",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={build_dir}",
            f'-DPYBIND11_PYTHON_VERSION="{python_version}"',
        ]

        for env_var in self.OPTIONAL_CMAKE_ENV_VARIABLES:
            if env_var in os.environ:
                cmake_args.append(f'-D{env_var}="{os.environ[env_var]}"')

        if not build_dir.exists():
            os.makedirs(build_dir)

        subprocess.run(
            f"cmake {cmake_list_dir} {' '.join(cmake_args)}",
            cwd=cmake_list_dir,
            shell=True,
            check=True,
        )

        subprocess.run(
            f"cmake --build . {build_args}",
            cwd=build_dir,
            shell=True,
            check=True,
        )


if __name__ == "__main__":
    setup(
        author="Hailo team",
        author_email="contact@hailo.ai",
        cmdclass={
            "bdist_wheel": bdist_wheel,
            "build_ext": build_ext,  # Build the C++ extension (_pyhailort) using cmake
            "install_lib": install_lib,  # Copy the extension to the install dir
        },
        description="HailoRT",
        entry_points={
            "console_scripts": [
                "hailo=hailo_platform.tools.hailocli.main:main",
            ]
        },
        ext_modules=[
            Extension("_pyhailort", sources=[]),
        ],
        install_requires=[
            "argcomplete",
            "contextlib2",
            "future",
            "netaddr",
            "netifaces",
            "numpy<2",
        ],
        name="hailort",
        package_data={
            "hailo_platform": [
                "../hailo_tutorials/notebooks/*",
                "../hailo_tutorials/hefs/*",
            ]
        },
        packages=find_packages(),
        platforms=[
            "linux_x86_64",
            "linux_aarch64",
        ],
        url="https://hailo.ai/",
        version="4.23.0",
        zip_safe=False,
    )
