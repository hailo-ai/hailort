#!/bin/bash

readonly max_processes_count=8
readonly default_processes_count=2

function print_usage {
    echo "Usage: [-h help] [-n]"
    echo "  -h    Print usage and exit"
    echo "  -n    Number of processes to run example. Max is $max_processes_count (defualt is $default_processes_count)"
}

process_count=$default_processes_count
while getopts "hn:" opt; do
    case "${opt}" in
        n) process_count=${OPTARG} ;;
        \?) echo "Try -h' for more information." ; exit 1 ;;
        h) print_usage; exit 0 ;;
    esac
done

if (( $process_count > $max_processes_count ))
then
    echo "Max processes to run is $max_processes_count! Got $process_count"
    exit 1
fi

for i in $(seq 0 $((process_count - 1)))
do
    ./build/cpp/multi_process_example/cpp_multi_process_example &
done

wait 
exit 0