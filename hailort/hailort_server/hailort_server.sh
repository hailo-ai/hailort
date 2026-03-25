#!/bin/bash
 
### BEGIN INIT INFO
# Provides:          hailort_server
# Required-Start:    $local_fs $network
# Required-Stop:     $local_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: hailort_server service
# Description:       Run hailort_server daemon 
### END INIT INFO

# TODO: Remove this file once the hailort_server will use systemd
# Carry out specific functions when asked to by the system

HRT_SERVER_ENV_FILE="/etc/default/hailort_server"
HRT_SERVER_STATIC_IP="192.168.10.12"
HRT_SERVER_ETH_INTERFACE="usb0"

if [ -f "$HRT_SERVER_ENV_FILE" ]; then
    echo "Sourcing environment variables from $HRT_SERVER_ENV_FILE"
    . "$HRT_SERVER_ENV_FILE"
fi

HRT_SERVER_ENV=(
    "HAILO_PRINT_TO_SYSLOG=1"
    "HAILO_DDR_ACTION_LIST=1"
)
HRT_SERVER_BIN=/usr/bin/hailort_server
HRT_SERVER_ARG=""

hrt_server_setup_eth () {
    ip link set "$HRT_SERVER_ETH_INTERFACE" up
    ip addr flush dev "$HRT_SERVER_ETH_INTERFACE"
    ip addr add "$HRT_SERVER_STATIC_IP/24" dev "$HRT_SERVER_ETH_INTERFACE"

    HRT_SERVER_ARG="$HRT_SERVER_STATIC_IP"
}

hrt_server_setup_usb () {
    /usr/bin/hailort_usb_setup.sh setup
    /usr/bin/hailort_usb_flicker.sh &

    HRT_SERVER_ARG="usb"
}

hrt_server_start () {
    if ip link show "$HRT_SERVER_ETH_INTERFACE" > /dev/null 2>&1; then
        echo "Starting hailort_server in ETH mode"
        hrt_server_setup_eth
    elif [ "$HAILO_SERVER_ADDRESS" == "usb" ]; then
        echo "Starting hailort_server in USB mode"
        hrt_server_setup_usb
    elif [ -n "$HAILO_SERVER_ADDRESS" ]; then
        echo "Starting hailort_server with user-arg: $HAILO_SERVER_ADDRESS"
        HRT_SERVER_ARG="$HAILO_SERVER_ADDRESS"
    else
        echo "Starting hailort_server in PCIE mode"
        HRT_SERVER_ARG="pcie"
    fi

    bash -c "while true; do ${HRT_SERVER_ENV[*]} $HRT_SERVER_BIN $HRT_SERVER_ARG; done &"
}

hrt_server_stop () {
    echo "Stopping hailort_server..."

    local pids=`pgrep -f $HRT_SERVER_BIN`
    kill ${pids[*]} > /dev/null 2>&1

    local retries=10
    for i in `seq 1 $retries`; do
        if pgrep -f $HRT_SERVER_BIN > /dev/null 2>&1; then
            sleep 1
        else
            echo "Done"
            return
        fi
    done

    echo "Timed-out waiting for hailort_server to shutdown"
}

case "$1" in
    start)
        hrt_server_start
        ;;
    stop)
        hrt_server_stop
        ;;
    restart)
        hrt_server_stop
        hrt_server_start
        ;;
    *)
        echo "Usage: /etc/init.d/hailort_server.sh {start|stop|restart}"
        exit 1
        ;;
esac
 
exit 0
