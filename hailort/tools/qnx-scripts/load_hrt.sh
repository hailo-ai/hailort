#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/qnx_env_vars.sh

cd $local_platform_sw_path
build_config=release
source /home/hailo/qnx710/qnxsdp-env.sh
./build.sh -k qnx -n pG -aaarch64 -b$build_config install

sshpass -p root scp lib/qnx.aarch64.$build_config/libhailort.* hrt-qnx:/home/hailo/
sshpass -p root scp bin/qnx.aarch64.$build_config/hailortcli hrt-qnx:/home/hailo
sshpass -p root scp bin/qnx.aarch64.$build_config/debalex hrt-qnx:/home/hailo
sshpass -p root scp bin/qnx.aarch64.$build_config/board_tests hrt-qnx:/home/hailo
