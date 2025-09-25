# Examples
The following examples are provided, demonstrating the HailoRT API:
- C examples:
  - `vstreams_example` - Basic inference of a shortcut network (inputs are sent through the device and right back out, without any changes made to the data):
    - Configure and activate network group and virtual streams.
    - The data is sent to the device via input vstreams and received via output vstreams.
    - The data is transformed before sent and after receiving in a different thread using the virtual stream pipeline.
  - `multi_device_example` - Basic inference of a shortcut network (inputs are sent through the device and right back out, without any changes made to the data), using VDevice over multiple pcie devices:
    - Configure and activate network group and virtual streams.
    - The data is sent to the device via input vstreams and received via output vstreams.
    - The data is transformed before sent and after receiving in a different thread using the virtual stream pipeline.
  - `multi_network_vstream_example` - Demonstrates how to work with multiple networks in a network group, using virtual streams.
    - The example works with an HEF that contains one network group, and two networks in the network group.
    - Configure the network group and set the batch size for each network.
    - Get the networks information to create the vstreams for each network.
    - The data is sent to the device via input vstreams and received via output vstreams.
    - The data is transformed before sent and after receiving in a different thread using the virtual stream pipeline.
  - `switch_network_groups_example` - Demonstrates how to work with multiple HEFs using virtual streams and HailoRT Model Scheduler for automatic network group switching.
    - This example uses pcie devices.
  - `switch_network_groups_manually_example` - Demonstrates how to work with multiple single input single output HEFs, switching the created network groups manually, using virtual streams.
  - `data_quantization_example` - Demonstrates how to set input/output stream params so as to allow for custom quantization:
    - Input streams may be marked as quantized, so that input data will not to be automatically quantized by the HailoRT library.
    - Output streams may be marked as quantized, so that output data will remain quantized (as it is after exiting the device by default), and won't be 'de-quantized' by the HailoRT library.
    - This example uses pcie devices.
  - `infer_pipeline_example` - Basic inference of a shortcut network using inference pipeline (blocking) api.
    - this example uses udp device.
  - `raw_streams_example` - Basic inference of a shortcut network using raw stream api.
    - The data is transformed before sent and after received in the same thread sending/receiving using the transformation api.
  - `raw_async_streams_single_thread_example` - Basic inference of a shortcut network using raw stream async api with
      a single thread.
    - Each async read operation will re-launch some new async read operation.
    - Each async write operation will re-launch some new async write operation.
    - The main thread will stop the async operations by deactivating the network group.
  - `notification_callback_example` - Demonstrates how to work with notification callbacks.

- C++ examples:
  - `vstreams_example` - Basic inference of a shortcut network, same as `vstreams_example` C example, uses HailoRT C++ api.
  - `multi_device_example` - Basic inference of a shortcut network over multiple devices, same as `multi_device_example` C example, uses HailoRT C++ api.
  - `multi_network_vstream_example` - Demonstrates how to work with multiple networks in a network group, same as `multi_network_vstream_example ` C example, uses HailoRT C++ api.
  - `switch_network_groups_example` - Demonstrates how to work with multiple HEFs using virtual streams and HailoRT Model Scheduler, same as `switch_network_groups_example ` C example, uses HailoRT C++ api.
  - `switch_network_groups_manually_example` -Demonstrates how to work with multiple HEFs, switching the running network_groups manually, with performance optimizations for I/O threads re-usage instead of re-creation at each network group activation. Uses C++ api.
  - `infer_streams_example` - Basic inference of a shortcut network, same as `raw_streams_example` C example, uses HailoRT C++ api.
  - `infer_pipeline_example` - Basic inference of a shortcut network using inference pipeline (blocking) api.
    - same as `infer_pipeline_example` C example, uses HailoRT C++ api.
  - `raw_streams_example` - Basic inference of a shortcut network, same as `raw_streams_example` C example, uses HailoRT C++ api.
  - `raw_async_streams_single_thread_example` - Basic inference of a shortcut network using raw stream async api with
      a single thread.
    - Each async read operation will re-launch some new async read operation.
    - Each async write operation will re-launch some new async write operation.
    - The main thread will stop the async operations by deactivating the network group.
  - `raw_async_streams_multi_thread_example` - Basic inference of a shortcut network using raw stream async api with
      a thread for each stream.
    - The threads will continuously initiate an async read or write operations.
    - The main thread will stop the async operations and the threads by deactivating the network group.
  - `multi_process_example` - Demonstrates how to work with HailoRT multi-process service and using the HailoRT Model Scheduler for network groups switching.
  Using the script `multi_process_example.sh` / `multi_process_example.ps1` one can specify the number of processes to run each hef, see `multi_process_example.sh -h`  / `multi_process_example.ps1 -h` for more information.
    - For Windows, in case of restricted execution policy, either change the policy, or run the script with "PowerShell -NoProfile -ExecutionPolicy Bypass -File <FilePath>"
  - `notification_callback_example` - Demonstrates how to work with notification callbacks, same as `notification_callback_example` C example.
You can find more details about each example in the HailoRT user guide.
  - `async_infer_basic_example` - Basic asynchronous inference of a multiple input and output model, uses HailoRT C++ api.
  - `async_infer_advanced_example` - More advanced asynchronous inference of a multi planar model, uses HailoRT C++ api.
## Compiling with CMake
Examples are configured and compiled using the following commands:
```sh
cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release
```
> **_NOTE:_** Write permissions are required to compile the examples from their current directory.
If this is not the case, copy the examples directory to another location with the required permissions.

In order to compile a specific example, add the example name as target with a c/cpp prefix:
```sh
cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release --target cpp_vstreams_example
```

## Running the examples

Before running an example, download the HEFs using the [download script](../../scripts/download_hefs.sh) from the scripts directory:
  ```sh
  cd ../../scripts
  ./download_hefs.sh
  ```

To run an example, use (from this examples directory):

  ```sh
  build/<c/cpp>/<example_name>/<example_name> [params..]
  ```

## Hailo Application Code Examples

The examples in this page are for demonstrating HailoRT API usage.

Hailo also offers an additional set of
[Application Code Examples](https://github.com/hailo-ai/Hailo-Application-Code-Examples),
which are more application-oriented.