"""
builds hailo_platform python package and its C++ dependencies using cmake
"""
import platform
import os
import subprocess
import sys

from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext as orig_build_ext
from wheel.bdist_wheel import bdist_wheel as orig_bdist_wheel


_plat_name = None
def _fix_plat_name(s):
    # plat_name does not require the "linux_" prefix
    return s.replace(platform.processor(), _plat_name.replace("linux_", ""))


class bdist_wheel(orig_bdist_wheel):
    """makes the wheel platform-dependant so it can be based on the _pyhailort architecture"""
    def finalize_options(self):
        # Save the plat_name option and pass it along to build_ext which will use it to change the processor in the
        # extension name.
        # All other paths will still use the naive processor, but that's ok, since the only thing that is packed into 
        # the wheel is the actual shared library, so only its name is relevant. Fixing all paths will require tweaking
        # build_py, install, install_lib commands or fixing this somehow all accross setuptools
        global _plat_name
        _plat_name = self.plat_name
        orig_bdist_wheel.finalize_options(self)
        self.root_is_pure = False


class build_ext(orig_build_ext):
    OPTIONAL_CMAKE_ENV_VARIABLES = [
        "CMAKE_TOOLCHAIN_FILE",
        "HAILORT_INCLUDE_DIR",
        "LIBHAILORT_PATH",
        "PYTHON_INCLUDE_DIRS",
        "CMAKE_GENERATOR",
        "PYTHON_LIBRARY",
    ]

    """defines a cmake command that will be called from the python build process"""
    def run(self):
        cfg = 'Debug' if self.debug else 'Release'

        build_args = f"--config {cfg}"
        build_directory = os.path.abspath(self.build_temp)
        cmake_list_dir = Path(__file__).absolute().parents[1] / "src"
        python_version = f"{sys.version_info.major}.{sys.version_info.minor}"

        cmake_args = [
            f'-DCMAKE_BUILD_TYPE={cfg}',
            f'-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={build_directory}',
            f'-DPYBIND11_PYTHON_VERSION={python_version}',
            f'-DPYTHON_EXECUTABLE={sys.executable}',
        ]

        for env_var in self.OPTIONAL_CMAKE_ENV_VARIABLES:
            if env_var in os.environ:
                if env_var == "CMAKE_GENERATOR":
                    cmake_args.append(f'-G "{os.environ[env_var]}"')
                else:
                    cmake_args.append(f"-D{env_var}={os.environ[env_var]}")

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.run(
            f"cmake {cmake_list_dir} {' '.join(cmake_args)}",
            cwd=self.build_temp,
            shell=True,
            check=True
        )

        subprocess.run(
            f"cmake --build . {build_args}",
            cwd=self.build_temp,
            shell=True,
            check=True,
        )

        for ext in self.extensions:
            ext_filename = self.get_ext_filename(ext.name)
            if platform.system() == "Linux" and _plat_name:
                ext_filename = _fix_plat_name(ext_filename)

            dst = Path(self.get_ext_fullpath(ext.name)).resolve().parent / "hailo_platform/pyhailort/"

            build_temp = Path(self.build_temp).resolve()
            if os.name == "nt":
                src = build_temp / cfg / ext_filename
            else:
                src = build_temp / ext_filename

            self.copy_file(src, dst)


if __name__ == "__main__":
    setup(
        author="Hailo team",
        author_email="contact@hailo.ai",
        cmdclass={
            "bdist_wheel": bdist_wheel,
            "build_ext": build_ext, # Build the C++ extension (_pyhailort) using cmake
        },
        description="HailoRT",
        entry_points={
            "console_scripts": [
                "hailo=hailo_platform.tools.hailocli.main:main",
            ]
        },
        ext_modules= [
            Extension('_pyhailort', sources=[]),
        ],
        install_requires=[
            "argcomplete",
            "contextlib2",
            "future",
            "netaddr",
            "netifaces",
            "verboselogs",
            # Pinned versions
            "numpy==1.23.3",
        ],
        name="hailort",
        package_data={
            "hailo_platform": [
                "../hailo_tutorials/notebooks/*",
                "../hailo_tutorials/hefs/*"
            ]
        },
        packages=find_packages(),
        platforms=[
            "linux_x86_64",
            "linux_aarch64",
        ],
        url="https://hailo.ai/",
        version="4.18.0",
        zip_safe=False,
    )
