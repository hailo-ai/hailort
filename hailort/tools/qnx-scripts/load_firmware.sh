#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/qnx_env_vars.sh

cd $local_platform_sw_path
./install.sh comp build_fw --hw-arch sage_b0 
sshpass -p root scp firmware/build/hailo8_fw.bin hrt-qnx:/home/hailo
