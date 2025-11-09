"""
Builds hailo_platform python package and its C++ dependencies using cmake
"""

import logging
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import Extension, find_packages, setup
from setuptools.command.build_ext import build_ext as orig_build_ext
from setuptools.command.install_lib import install_lib as orig_install_lib
from wheel.bdist_wheel import bdist_wheel as orig_bdist_wheel

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(module)s.%(funcName)s:%(lineno)d - %(message)s",
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler(".setup.log"),
    ],
)

for handler in logging.getLogger().handlers:
    if isinstance(handler, logging.StreamHandler):
        handler.setLevel(logging.ERROR)

# Global variables
_BUILD_TYPE = os.environ.get("CMAKE_BUILD_TYPE", "Release")
_PY_VERSION = f"{sys.version_info.major}.{sys.version_info.minor}"
_plat_name = ""
_logger = logging.getLogger("setup")


def init_plat_name():
    """Find the platform name from the command line arguments."""
    for i, arg in enumerate(sys.argv):
        if arg == "--plat-name" and i + 1 < len(sys.argv):
            global _plat_name
            _plat_name = sys.argv[i + 1]
            _logger.info(f"Found --plat-name argument: {_plat_name}")
            break


def get_arch(plat_name):
    """Find the architecture from the platform name or environment variables, or use the native one."""
    os_name = platform.system().lower()
    arch = re.sub(f"{os_name}[-_]" or "", "", plat_name)
    return arch


class install_lib(orig_install_lib):
    """ """

    def _search_lib(self, current_dir, exact_match, fallback_match):
        """
            Search for the correct library in the build dir.
            Handle cases of single and multi config build trees.
            If no exact candidate is found, re-check without the architecture part.
        Args:
            current_dir (pathlib.Path):
            exact_match (str): expected regex for the library name
            fallback_match (str): fallback regex for the library name

        Returns:
             the path to the library (str)

        Raises:
            ValueError:
        """
        for lib_name in (
            exact_match,
            fallback_match,
        ):
            for build_dir in (
                f"build*{_BUILD_TYPE}*",
                f"build*{_BUILD_TYPE}*/{_BUILD_TYPE}",
            ):
                lib_regex = f"{build_dir}/{lib_name}"
                _logger.info(f"Searching lib in {lib_regex}")
                candidates = list(current_dir.rglob(lib_regex))

                if len(candidates) == 1:
                    lib = str(candidates[0])
                    return lib
                elif len(candidates) == 0:
                    _logger.warning(f"Could not find lib with regex {lib_regex}")
                else:
                    _logger.warning(
                        f"Found multiple libs with {lib_regex}: {candidates}"
                    )
        else:
            raise ValueError("Could not find the extension library")

    def install(self):
        """
        When cross compiling, the extension is not automatically copied into
        the install dir, and therefore it needs to be done manually.
        """
        outfiles = super().install()

        arch = get_arch(_plat_name)
        py_str = _PY_VERSION.replace(".", "")
        extension = "pyd" if os.name == "nt" else "so"
        current_dir = Path(__file__).parent.absolute()
        dst = os.path.join(self.install_dir, "hailo_platform", "pyhailort")

        lib = self._search_lib(
            current_dir,
            f"_pyhailort*{py_str}*{arch}*{extension}",
            f"_pyhailort*{py_str}*{extension}",
        )
        _logger.info(f"Copying extension {lib} -> {dst}")
        shutil.copy2(lib, dst)
        return outfiles


class bdist_wheel(orig_bdist_wheel):
    def finalize_options(self):
        """
        Force the wheel name to include the platform name based on the
        extension module.
        """
        super().finalize_options()
        self.root_is_pure = False

        if _plat_name:
            _logger.info(f"Setting plat_name to {_plat_name}")
            self.plat_name = _plat_name


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

    def finalize_options(self):
        super().finalize_options()

        global _plat_name
        if not _plat_name:
            if os.environ.get("LIBHAILORT_PATH"):
                lib_path = Path(os.environ["LIBHAILORT_PATH"])
                dir_name = lib_path.parent.name

                # check if we are multi config build tree. If so, move another level up
                if dir_name.lower() == _BUILD_TYPE.lower():
                    dir_name = lib_path.parent.parent.name

                # the dir name will be <os>.<arch>.<build_type>. extract the arch part
                os_name, arch_name = [s.lower() for s in dir_name.split(".")[:2]]
                _logger.info(
                    f"inferred plat_name from LIBHAILORT_PATH ({dir_name} -> os={os_name}, arch={arch_name})"
                )
            else:
                os_name, arch_name = (
                    platform.system().lower(),
                    platform.machine().lower(),
                )
                _logger.warning(
                    f"plat_name is not set and cannot be inferred from environment variables. Setting plat_name to the native platform (os={os_name}, arch={arch_name})"
                )

            _plat_name = f"{os_name}_{arch_name}"
            logging.info(f"Set plat_name: {_plat_name}")

        self.plat_name = _plat_name

    def run(self):
        """
        Defines a cmake command that will be called from the python build process.
        The cmake command will build the C++ extension (_pyhailort) and install it.
        Multiple CMake variables can be passed as environment variables to
        control the target library.
        """
        build_args = f"--parallel --config {_BUILD_TYPE} --target install"
        _logger.info(f"build args: {build_args}")

        current_dir = Path(__file__).parent.absolute()
        cmake_list_dir = current_dir.parent / "src"
        dst_arch = get_arch(_plat_name)
        build_dir = current_dir / f"build.{dst_arch}.{_BUILD_TYPE}"

        cmake_args = [
            f"-B{build_dir}",
            f"-DCMAKE_BUILD_TYPE={_BUILD_TYPE}",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={build_dir}",
            f'-DPYBIND11_PYTHON_VERSION="{_PY_VERSION}"',
        ]
        _logger.info(f"cmake args: {cmake_args}")

        for env_var in self.OPTIONAL_CMAKE_ENV_VARIABLES:
            if env_var in os.environ:
                cmake_args.append(f'-D{env_var}="{os.environ[env_var]}"')
                _logger.info(f"cmake env var: {env_var}={os.environ[env_var]}")

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


init_plat_name()


if __name__ == "__main__":
    setup(
        author="Hailo team",
        author_email="contact@hailo.ai",
        cmdclass={
            "bdist_wheel": bdist_wheel,
            # Build the C++ extension (_pyhailort) using cmake
            "build_ext": build_ext,
            "install_lib": install_lib,  # Copy the extension to the install dir
        },
        description="HailoRT",
        ext_modules=[
            Extension("_pyhailort", sources=[]),
        ],
        install_requires=[
            "numpy<2" if sys.version_info < (3, 13) else "numpy",
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
        version="5.1.1",
        zip_safe=False,
    )
