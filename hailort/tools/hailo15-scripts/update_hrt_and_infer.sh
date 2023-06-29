#! /bin/bash
set -e

# Include Environment declarations
script_directory=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source "$script_directory"/hailo15_env_vars.sh

# Build hailo15 artifacts
/bin/bash "$script_directory"/load_hrt.sh

# Build hailo15 PCR
/bin/bash "$script_directory"/load_pcr.sh

# Build hailo15 fw
cd $local_platform_sw_path
./install.sh comp build_fw --fw vpu --hw-arch hailo15
scp firmware/vpu_firmware/build/hailo15_nnc_fw.bin root@$h15:/lib/firmware/hailo/hailo15_nnc_fw.bin

# Build integrated_nnc (hailo15) driver
/bin/bash "$script_directory"/load_driver.sh

# Run sanity infer
/bin/bash "$script_directory"/sanity_infer.sh
