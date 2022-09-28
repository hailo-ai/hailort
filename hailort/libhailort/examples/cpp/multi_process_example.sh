#!/bin/bash

readonly first_hef="hefs/multi_network_shortcut_net.hef"
readonly second_hef="hefs/shortcut_net.hef"
readonly max_processes_count=8
readonly default_processes_count=1

function print_usage {
    echo "Usage: [-h help] [-n] [-m]"
    echo "  -h    Print usage and exit"
    echo "  -n    Number of processes to run example with $first_hef. Max is $max_processes_count (defualt is $default_processes_count)"
    echo "  -m    Number of processes to run example with $second_hef. Max is $max_processes_count (defualt is $default_processes_count)"
}

first_hef_count=1
second_hef_count=1
while getopts "hn:m:" opt; do
    case "${opt}" in
        n) first_hef_count=${OPTARG} ;;
        m) second_hef_count=${OPTARG} ;;
        \?) echo "Try -h' for more information." ; exit 1 ;;
        h) print_usage; exit 0 ;;
    esac
done

if (( $first_hef_count > $max_processes_count ))
then
    echo "Max processes to run each hef is $max_processes_count! Given $first_hef_count for $first_hef"
    exit 1
fi

if (( $second_hef_count > $max_processes_count ))
then
    echo "Max processes to run each hef is $max_processes_count! Given $second_hef_count for $second_hef"
    exit 1
fi

# Check service is enabled
service hailort status | grep 'disabled;' > /dev/null 2>&1
if [ $? == 0 ]
then
    echo "HailoRT service is not enabled."
    echo "To enable and start the service run the following command: 'sudo systemctl enable --now hailort.service'"
    exit 1
fi

# Check service is active
service hailort status | grep 'active (running)' > /dev/null 2>&1
if [ $? != 0 ]
then
    echo "HailoRT service is not active."
    echo "To start the service run the following command: 'sudo systemctl start hailort.service'"
    exit 1
fi

max=$(( $first_hef_count > $second_hef_count ? $first_hef_count : $second_hef_count ))
for i in $(seq 0 $max)
do
    if (( $i < $first_hef_count))
    then
        ./build/cpp/cpp_multi_process_example $first_hef &
    fi

    if (( $i < $second_hef_count))
    then
        ./build/cpp/cpp_multi_process_example $second_hef &
    fi
done

wait 
exit 0