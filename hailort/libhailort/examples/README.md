# Examples
For a thorough explanation on the examples strcture and usage, refer to HailoRT user-guide in the developer zone - ``https://hailo.ai/developer-zone/documentation/hailort/latest/``.

The following examples below demonstrate the HailoRT API:

> **_NOTE:_** The C examples cannot be run on Hailo-10H.
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
    - Input streams may be marked as quantized, so that the input data will not to be automatically quantized by the HailoRT library.
    - Output streams may be marked as quantized, so that the output data will remain quantized (as it is after exiting the device by default), and won't be 'de-quantized' by the HailoRT library.
    - This example uses pcie devices.
  - `infer_pipeline_example` - Basic inference of a shortcut network using inference pipeline (blocking) API.
    - this example uses udp device.
  - `raw_streams_example` - Basic inference of a shortcut network using raw stream API.
    - The data is transformed before being sent and after it has been received in the same thread sending/receiving using the transformation API.
  - `raw_async_streams_single_thread_example` - Basic inference of a shortcut network using raw stream async API with
      a single thread.
    - Each async read operation will re-launch some new async read operation.
    - Each async write operation will re-launch some new async write operation.
    - The main thread will stop the async operations by deactivating the network group.
  - `notification_callback_example` - Demonstrates how to work with notification callbacks.

- C++ examples:
  - `async_infer_advanced_example` - More advanced asynchronous inference of a multi planar model, uses HailoRT C++ API.
  - `async_infer_basic_example` - Basic asynchronous inference of a multiple input and output model, uses HailoRT C++ API.
  - `async_infer_dma_buffer_example` - asynchronous inference of a model using DMA buffers, uses HailoRT C++ API.
  - `notification_callback_example` - Demonstrates how to work with notification callbacks, same as `notification_callback_example` C example.
  - `power_measurement_example` - Demonstrates how to perform a continuous power measurement on the device.
  - `query_performance_and_health_stats_example` - Demonstrates how to get performance and health queries from the device.
  - `multi_model_inference_example` - Demonstrates how to run multiple models using the HailoRT C++ API.

- GenAI examples:
  - `chat_example` - Demonstrates LLM-based chat application using HailoRT C++ API.
  - `vlm_example` - Demonstrates VLM usage in a chat application using HailoRT C++ API.
  - `speech2text_example` - Demonstrates Speech2Text usage for audio transcription using HailoRT C++ API.

## Compiling with CMake
Examples are configured and compiled using the following commands:
```sh
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release
```
> **_NOTE:_** Write permissions are required to compile the examples from their current directory.
If this is not the case, copy the examples directory to another location with the required permissions.

In order to compile a specific example, add the example name as target with a c/cpp prefix:
```sh
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release --target cpp_async_infer_basic_example
```

## Running the examples

Before running an example, download the HEFs using the [download script](../../scripts/download_hefs.sh) from the scripts directory:
  ```sh
  cd ../../scripts
  ./download_hefs.sh
  ```
> **_NOTE:_** GenAI HEFs are available in Hailo Model Zoo GenAI repository: ``https://github.com/hailo-ai/hailo_model_zoo_genai.git``.

To run an example, use (from this examples directory):

  ```sh
  build/<c/cpp>/<example_name>/<example_name> [params..]
  ```

## Hailo Application Code Examples

The examples in this page are for demonstrating HailoRT API usage.

Hailo also offers an additional set of
[Application Code Examples](https://github.com/hailo-ai/Hailo-Application-Code-Examples),
which are more application-oriented.
