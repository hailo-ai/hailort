#!/usr/bin/env bash

# Set bash to exit if function fails
set -e

function validate_interface_exists() {
    if [[ -z $1 ]] ; then
        echo "Usage: $0 interface_name"
        return 1
    fi
    if ip a show $1 up > /dev/null 2>&1; then
        echo "Working on interface $1"
        return 0
    else
        echo "Interface $1 not found"
        return 2
    fi
}


function main() {
    validate_interface_exists "$@"

    echo
    echo "Network stack errors"
    nstat -az | grep "Udp.*.Errors\|Discards" | awk 'NF{NF--};1'


    echo
    echo "Driver rx & tx errors"
    ethtool -S $1 | grep -G ".*.err\|.*.fail\|.*.drop\|.*.miss" | awk '{$1=$1};1'
}


main "$@"
