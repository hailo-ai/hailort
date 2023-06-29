#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

cd $local_platform_sw_path
./install.sh comp build_fw --fw vpu --hw-arch hailo15
scp firmware/vpu_firmware/build/hailo15_nnc_fw.bin root@$h15:/lib/firmware/hailo/hailo15_nnc_fw.bin
ssh root@$h15 "modprobe -r hailo_integrated_nnc && modprobe hailo_integrated_nnc"
