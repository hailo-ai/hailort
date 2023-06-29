#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

ssh root@$h15 "hailortcli run /etc/hailo/hefs/hailo15/shortcut_net/28_28_3/shortcut_net.hef -c 1" 
