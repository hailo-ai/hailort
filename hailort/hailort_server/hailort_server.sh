#! /bin/bash
 
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

readonly ENV_FILE="/etc/default/hailort_server"
readonly STATIC_IP="192.168.10.12"
readonly INTERFACE="usb0"
if [ -f "$ENV_FILE" ]; then
    echo "Sourcing environment variables from $ENV_FILE"
    . "$ENV_FILE"
fi

case "$1" in
  start)
    echo "Starting hailort_server"
    if ip link show "$INTERFACE" > /dev/null 2>&1; then
        echo "Detected $INTERFACE - assigning IP $STATIC_IP"
        ip link set "$INTERFACE" up
        ip addr flush dev "$INTERFACE"
        ip addr add "$STATIC_IP/24" dev "$INTERFACE"
        SERVER_ADDRESS_ARG="$STATIC_IP"
    elif [ -z "$HAILO_SERVER_ADDRESS" ]; then
        SERVER_ADDRESS_ARG=""
    else
        SERVER_ADDRESS_ARG="$HAILO_SERVER_ADDRESS"
    fi
    ENV="HAILO_MONITOR=1 HAILO_MONITOR_TIME_INTERVAL=100 HAILO_PRINT_TO_SYSLOG=1 HAILO_DDR_ACTION_LIST=1"
    bash -c "while true; do ${ENV} /usr/bin/hailort_server ${SERVER_ADDRESS_ARG}; done &"
    ;;
  stop)
    echo "Stopping hailort_server..."
    bash -c "kill `pgrep -f '/usr/bin/hailort_server' | tr '\n' ' '`"
    ;;
  restart)
    $0 stop
    sleep 1
    $0 start
    ;;
  *)
    echo "Usage: /etc/init.d/hailort_server.sh {start|stop|restart}"
    exit 1
    ;;
esac
 
exit 0
