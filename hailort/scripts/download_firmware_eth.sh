#!/bin/bash
set -e

readonly BASE_URI="https://hailo-hailort.s3.eu-west-2.amazonaws.com"
readonly HRT_VERSION=4.14.0
readonly FW_AWS_DIR="Hailo8/${HRT_VERSION}/FW"
readonly FW="hailo8_fw.${HRT_VERSION}_eth.bin"

function download_fw(){
    wget -N ${BASE_URI}/${FW_AWS_DIR}/${FW}
}

function main(){
    download_fw
}

main
