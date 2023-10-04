#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

cd $local_platform_sw_path
source hailo_platform_venv/bin/activate
ssh root@$h15 "hailortcli fw-logger /tmp/fw_log.dat"
scp root@$h15:/tmp/fw_log.dat /tmp
ssh root@$h15 "rm /tmp/fw_log.dat"

python ./platform_internals/hailo_platform_internals/tools/firmware/tracelog_parser_tool/tracelog_parser_tool/parse_tracelog.py --fw vpu --core-log-entries firmware/vpu_firmware/build/hailo15_nnc_fw_*_log_entries.csv --core-only --raw-input-file /tmp/fw_log.dat

