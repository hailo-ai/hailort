#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

cd $local_platform_sw_path
# Compile PCR
./install.sh comp build_infra_tools --arch aarch64 --build-hailort --build-type release

scp platform_internals/hailo_platform_internals/low_level_tools/build/linux.aarch64.release/pcr/pcr root@$h15:/usr/bin/
