#!/usr/bin/env bash

# Setting the script to exit when command returns a non-zero status
set -e
# Default interface IP
INTERFACE_IP="10.0.0.50/24"

function parse_commandline_arguments() {
    if [ -z $1 ] ; then
        echo "Usage: $0 interface [-i|--ip INTERFACE_IP]"
        return 1
    fi

    INTERFACE_NAME=$1
    # Shifting since we already processed the interface name
    shift

    while [[ "$#" -gt 0 ]]; do
        case $1 in
            -i|--ip) INTERFACE_IP=$2; shift ;;
            *) echo "Unknown parameter passed: $1"; exit 1;;
        esac
        shift
    done
}

function validate_interface_exists() {
    if ip a show $1 up > /dev/null 2>&1; then
        echo "Configuring interface $1"
        return 0
    else
        echo "Interface $1 not found"
        return 2
    fi
}

function set_interface_ip() {
    # Piping to true because we don't care about the return value here
    CURRENT_INTERFACE_IP=`ip -4 addr show dev $INTERFACE | grep -oP '(?<=inet\s)\d+(\.\d+){3}/\d+' || true`
    # The interface might have multiple addresses, so we'll check if the address to set is one of them
    if [[ $CURRENT_INTERFACE_IP = *$INTERFACE_IP* ]]; then
        echo "Skipping setting an IP address since it is already configured"
        return 0
    fi
    echo "Configuring $INTERFACE IP to: $INTERFACE_IP"
    sudo ip -4 address add dev $INTERFACE $INTERFACE_IP
}


function configure_interface() {
    parse_commandline_arguments "$@"
    validate_interface_exists $INTERFACE_NAME
    INTERFACE=$INTERFACE_NAME

    # Shutting down the interface in order for kernel parameter changes to register
    sudo ip link set $INTERFACE down
    sudo sysctl -w net.ipv6.conf.$INTERFACE.disable_ipv6=1
    sudo sysctl -w net.ipv6.conf.$INTERFACE.autoconf=0

    set_interface_ip
    # After we had finished configuration, turn the interface back on
    sudo ip link set $INTERFACE up

    echo "interface $INTERFACE configured successfully"
}

configure_interface "$@"

