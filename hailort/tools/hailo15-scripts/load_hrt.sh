#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

cd $local_platform_sw_path
build_config=release
./build.sh -n pG -aaarch64 -b$build_config install

scp lib/linux.aarch64.$build_config/libhailort.* root@$h15:/usr/lib/
scp bin/linux.aarch64.$build_config/hailortcli root@$h15:/usr/bin/
scp bin/linux.aarch64.$build_config/debalex root@$h15:/usr/bin/
scp bin/linux.aarch64.$build_config/board_tests root@$h15:/usr/bin/
