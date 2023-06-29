#! /bin/bash
set -e

# Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
local_platform_sw_path="$script_directory"/../../../
h15="10.0.0.1"
ssh-copy-id root@$h15