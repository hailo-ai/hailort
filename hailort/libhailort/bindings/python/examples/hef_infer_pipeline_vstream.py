from hailo_platform import (HEF, VDevice, ConfigureParams, InferVStreams, InputVStreamParams,
    OutputVStreamParams, FormatType)
from hailo_platform.pyhailort.pyhailort import HailoStreamInterface
import numpy as np
import argparse

def parse_args():
    parser = argparse.ArgumentParser(description='Streaming API example')
    parser.add_argument('hef_path', type=str, help='Path of the HEF to run')
    parser.add_argument('-n', '--num-frames', type=int, default=10, help='Number of frames to send')
    return parser.parse_args()

def main():
    args = parse_args()
    with VDevice() as target:
        hef = HEF(args.hef_path)
        configure_params = ConfigureParams.create_from_hef(hef, interface=HailoStreamInterface.PCIe)
        network_groups = target.configure(hef, configure_params)
        network_group = network_groups[0]
        network_group_params = network_group.create_params()
        input_vstreams_params = InputVStreamParams.make(network_group, quantized=False, format_type=FormatType.FLOAT32)
        output_vstreams_params = OutputVStreamParams.make(network_group, quantized=True, format_type=FormatType.AUTO)
        with InferVStreams(network_group, input_vstreams_params, output_vstreams_params) as infer_pipeline:
            input_names_to_shape = {vstream_info.name: vstream_info.shape for vstream_info in hef.get_input_vstream_infos()}
            input_data = {name : 1 + np.ndarray([args.num_frames] + list(shape), dtype=np.float32) for name, shape in input_names_to_shape.items()}
            with network_group.activate(network_group_params):
                _ = infer_pipeline.infer(input_data)
                fps = args.num_frames / infer_pipeline.get_hw_time()

    print('Inference ran successfully')
    print(f'FPS: {fps}')

if __name__ == '__main__':
    main()