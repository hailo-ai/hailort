import argparse
from multiprocessing import Process

import numpy as np
from hailo_platform import (HEF, PcieDevice, HailoStreamInterface, ConfigureParams, InputVStreamParams, InputVStreams,
                            OutputVStreamParams, OutputVStreams)

def send(configured_network, num_frames):
    vstreams_params = InputVStreamParams.make(configured_network)
    configured_network.wait_for_activation(1000)
    with InputVStreams(configured_network, vstreams_params) as vstreams:
        vstream_to_buffer = {vstream: np.ndarray([1] + list(vstream.shape), dtype=vstream.dtype) for vstream in vstreams}
        for _ in range(num_frames):
            for vstream, buff in vstream_to_buffer.items():
                vstream.send(buff)
        # Flushing is not mandatory here
        for vstream in vstreams:
            vstream.flush()

def recv(configured_network, vstreams_params, num_frames):
    configured_network.wait_for_activation(1000)
    with OutputVStreams(configured_network, vstreams_params) as vstreams:
        for _ in range(num_frames):
            for vstream in vstreams:
                _ = vstream.recv()

def recv_all(configured_network, num_frames):
    vstreams_params_groups = OutputVStreamParams.make_groups(configured_network)
    recv_procs = []
    for vstreams_params in vstreams_params_groups:
        proc = Process(target=recv, args=(configured_network, vstreams_params, num_frames))
        proc.start()
        recv_procs.append(proc)

    for proc in recv_procs:
        proc.join()

def parse_args():
    parser = argparse.ArgumentParser(description='vStream API example')
    parser.add_argument('hef_path', type=str, help='Path of the HEF to run')
    parser.add_argument('-n', '--num-frames', type=int, default=1000, help='Number of frames to send')
    return parser.parse_args()

def main():
    args = parse_args()
    hef = HEF(args.hef_path)

    with PcieDevice() as device:
        configure_params = ConfigureParams.create_from_hef(hef, interface=HailoStreamInterface.PCIe)
        network_group = device.configure(hef, configure_params)[0]
        network_group_params = network_group.create_params()
        send_process = Process(target=send, args=(network_group, args.num_frames))
        recv_process = Process(target=recv_all, args=(network_group, args.num_frames))
        recv_process.start()
        send_process.start()
        with network_group.activate(network_group_params):
            send_process.join()
            recv_process.join()

if __name__ == '__main__':
    main()
