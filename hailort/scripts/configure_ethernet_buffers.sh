#!/usr/bin/env bash

# Setting the script to exit when command returns a non-zero status
set -e

declare -A increase_target_configurations=(
    ["rmem_max"]=26214400
    ["rmem_default"]=212992
    ["netdev_max_backlog"]=5000
)

declare -A decrease_target_configurations=(
    ["wmem_max"]=212992
    ["wmem_default"]=212992
)

function validate_interface_exists() {
    if [ -z $1 ] ; then
        echo "Usage: $0 interface"
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

function update_kernel_network_parameters() {
    echo "Updating kernel parameters. Resetting default values back in case someone changed them by accident"
    # These values should be increased if they are lower than expected
    for target_configuration in "${!increase_target_configurations[@]}"; do
        actual_configuration_value=`sysctl -b net.core.$target_configuration`
        target_configuration_value=${increase_target_configurations[$target_configuration]}
        if [[ $actual_configuration_value -gt $target_configuration_value ]]; then
            echo "Skipping $target_configuration, it is already configured to a higher value ($actual_configuration_value) than target ($target_configuration_value)";
            continue
        fi
        sysctl -w net.core.$target_configuration=${increase_target_configurations[$target_configuration]}
    done

    # These values should be decreased if they are higher than expected
    for target_configuration in "${!decrease_target_configurations[@]}"; do
        actual_configuration_value=`sysctl -b net.core.$target_configuration`
        target_configuration_value=${decrease_target_configurations[$target_configuration]}
        if [[ $actual_configuration_value -lt $target_configuration_value ]]; then
            echo "Skipping $target_configuration, it is already configured to a lower value ($actual_configuration_value) than target ($target_configuration_value)";
            continue
        fi
        sysctl -w net.core.$target_configuration=${decrease_target_configurations[$target_configuration]}
    done
}

function validate_ring_parameters_can_be_modified() {
    if ethtool -g $1 > /dev/null 2>&1; then
        return 0;
    else
        echo "NIC for interface $1 doesn't support modifying buffer sizes."
        echo "Successfully finished configuring interface $1 to the best of our ability"
        return 1
    fi
}

function update_network_driver_configuration() {
    # Make sure the ring parameters can be modified
    validate_ring_parameters_can_be_modified "$1"

    CURRENT_RX_VALUE=$(ethtool -g $1 | grep "RX:" | cut -f 3 | sed -n 2p)
    EXPECTED_RX_VALUE=$(ethtool -g $1 | grep "RX:" | cut -f 3 | sed -n 1p)

    if [[ ${CURRENT_RX_VALUE} -ne ${EXPECTED_RX_VALUE} ]]; then
       echo "Updating RX ring size to $EXPECTED_RX_VALUE"
       ethtool -G $1 rx $EXPECTED_RX_VALUE
    else
       echo "RX ring size is $EXPECTED_RX_VALUE. No change is required"
    fi
}

function update_ethernet_configuration() {
    validate_interface_exists "$@"
    INTERFACE=$1
    update_kernel_network_parameters
    update_network_driver_configuration $INTERFACE
    echo "Successfully finished configuring interface $INTERFACE"
}

(update_ethernet_configuration "$@")
