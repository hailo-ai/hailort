#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/qnx_env_vars.sh

cd $local_platform_sw_path
build_config=release
source /home/hailo/qnx710/qnxsdp-env.sh
./build.sh -k qnx -n pG -aaarch64 -b$build_config hailort-qnx-pcie-driver

sshpass -p root scp bin/qnx.aarch64.$build_config/hailort-qnx-pcie-driver hrt-qnx:/home/hailo

echo "*************************************************************************************"
echo "Driver file uploaded to QNX host.                                                    "
echo "Note: Please manually run the driver on the QNX by running ./hailort-qnx-pcie-driver."
echo "*************************************************************************************"

