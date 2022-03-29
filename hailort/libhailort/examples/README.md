# Examples
The following examples are provided, demonstrating the HailoRT API:
- C examples:
  - `vstreams_example` - Basic inference of a shortcut network (inputs are sent through the device and right back out, without any changes made to the data):
    - Configure and activate network group and virtual streams.
    - The data is sent to the device via input vstreams and received via output vstreams.
    - The data is transformed before sent and after receiving in a different thread using the virtual stream pipeline.
  - `raw_streams_example` - Basic inference of a shortcut network using raw stream api.
    - The data is transformed before sent and after received in the same thread sending/receiving using the transformation api.
  - `data_quantization_example` - Demonstrates how to set input/output stream params so as to allow for custom quantization:
    - Input streams may be marked as quantized, so that input data will not to be automatically quantized by the HailoRT library.
    - Output streams may be marked as quantized, so that output data will remain quantized (as it is after exiting the device by default), and won't be 'de-quantized' by the HailoRT library.
    - This example uses pcie devices.
  - `switch_hefs_example` - Demonstrates how to work with multiple HEFs using virtual streams.
    - This example uses pcie devices.
  - `switch_single_io_hefs_example` - Demonstrates how to work with multiple single input single output HEFs using virtual streams.
    - This example uses pcie devices.
  - `multi_network_vstream_example` - Demonstrates how to work with multiple networks in a network group, using virtual streams.
    - The example works with an HEF that contains one network group, and two networks in the network group.
    - Configure the network group and set the batch size for each network.
    - Get the networks information to create the vstreams for each network.
    - The data is sent to the device via input vstreams and received via output vstreams.
    - The data is transformed before sent and after receiving in a different thread using the virtual stream pipeline.
- C++ examples:
  - `vstreams_example` - Basic inference of a shortcut network, same as `vstreams_example` C example, uses HailoRT C++ api.
  - `raw_streams_example` - Basic inference of a shortcut network, same as `raw_streams_example` C example, uses HailoRT C++ api.
  - `multi_network_vstream_example` - Demonstrates how to work with multiple networks in a network group, same as `multi_network_vstream_example ` C example, uses HailoRT C++ api.
  - `switch_hefs_example` - Demonstrates how to work with multiple HEFs using virtual streams, same as `switch_hefs_example ` C example, uses HailoRT C++ api.
  - `switch_hefs_example_threads_reuse` - Same as `switch_hefs_example` CPP example, with performance optimizations for I/O threads re-usage instead of re-creation at each network group activation.

## Compiling with CMake
Examples are configured and compiled using the following commands:
```sh
cmake -H. -Bbuild
cmake --build build
```
> **_NOTE:_** Write permissions are required to compile the examples from their current directory.
If this is not the case, copy the examples directory to another location with the required permissions.

## Running the examples
One can run the example using the following commands, from the examples directory:

  ```sh
  build/<c/cpp>/<example_name> [params..]
  ```
