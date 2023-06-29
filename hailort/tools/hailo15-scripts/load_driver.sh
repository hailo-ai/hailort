#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

cd $local_platform_sw_path
./install.sh comp build_integrated_nnc_driver --image-path /local/bkc/v0.29-build-2023-05-07
path="$local_platform_sw_path"/hailort/drivers/linux/integrated_nnc/hailo_integrated_nnc.ko
scp $path root@$h15:/lib/modules/5.15.32-yocto-standard/kernel/drivers/misc/hailo_integrated_nnc.ko

ssh root@$h15 "modprobe -r hailo_integrated_nnc && modprobe hailo_integrated_nnc"
