#!/bin/bash
set -e

readonly BASE_URI="https://hailo-hailort.s3.eu-west-2.amazonaws.com"
readonly HRT_VERSION=5.1.1
readonly REMOTE_HEF_DIR="Hailo10H/${HRT_VERSION}/HEFS"
readonly LOCAL_EXAMPLES_HEF_DIR="../libhailort/examples/hefs"
readonly LOCAL_TUTORIALS_HEF_DIR="../libhailort/bindings/python/platform/hailo_tutorials/hefs"
readonly EXAMPLES_HEFS=(
    "shortcut_net.hef"
    "shortcut_net_nv12.hef"
    "multi_network_shortcut_net.hef"
)
readonly TUTORIALS_HEFS=(
    "resnet_v1_18.hef"
    "shortcut_net.hef"
)

function create_hef_dir(){
    for d in $LOCAL_EXAMPLES_HEF_DIR $LOCAL_TUTORIALS_HEF_DIR; do
        if ! [ -d ${d} ]; then
            mkdir -p ${d}
        fi
    done
}

function download_hefs(){
    for hef in "${EXAMPLES_HEFS[@]}"; do
        wget -N ${BASE_URI}/${REMOTE_HEF_DIR}/${hef} -O ${LOCAL_EXAMPLES_HEF_DIR}/${hef}
    done
    for hef in "${TUTORIALS_HEFS[@]}"; do
        wget -N ${BASE_URI}/${REMOTE_HEF_DIR}/${hef} -O ${LOCAL_TUTORIALS_HEF_DIR}/${hef}
    done
}

function main(){
    create_hef_dir
    download_hefs
}

main
