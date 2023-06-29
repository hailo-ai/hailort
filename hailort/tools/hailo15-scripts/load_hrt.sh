#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

cd $local_platform_sw_path
./build.sh -aaarch64 -brelease install

scp lib/linux.aarch64.release/libhailort.* root@$h15:/usr/lib/
scp bin/linux.aarch64.release/hailortcli root@$h15:/usr/bin/
scp bin/linux.aarch64.release/debalex root@$h15:/usr/bin/
scp bin/linux.aarch64.release/board_tests root@$h15:/usr/bin/
