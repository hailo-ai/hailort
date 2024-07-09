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
case "$1" in
  start)
    echo "Starting hailort_server"
    bash -c "cd /usr/bin && hailort_server &"
    ;;

  stop)
    echo "Stopping hailort_server..."
    bash -c "killall hailort_server"
    ;;

  restart)
    $0 stop
    sleep 1
    $0 start
    ;;

  *)
    echo "Usage: /etc/init.d/hailort_server {start|stop|restart}"
    exit 1
    ;;
esac
 
exit 0