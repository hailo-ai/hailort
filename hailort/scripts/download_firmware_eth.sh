#!/bin/bash
set -e

readonly BASE_URI="https://hailo-hailort.s3.eu-west-2.amazonaws.com"
readonly FW_AWS_DIR="Hailo8/4.17.0_dev/FW"
readonly FW="hailo8_fw.4.17.0_eth.bin"

function download_fw(){
    wget -N ${BASE_URI}/${FW_AWS_DIR}/${FW}
}

function main(){
    download_fw
}

main
