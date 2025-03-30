/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstreams_example
 * This example demonstrates using virtual streams over c++
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <thread>


#define HEF_FILE ("hefs/shortcut_net.hef")
constexpr size_t FRAMES_COUNT = 100;
constexpr size_t MAX_LAYER_EDGES = 16;

using namespace hailort;

Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice)
{
    auto hef = Hef::create(HEF_FILE);
    if (!hef) {
        return make_unexpected(hef.status());
    }

    auto configure_params = vdevice.create_configure_params(hef.value());
    if (!configure_params) {
        return make_unexpected(configure_params.status());
    }

    auto network_groups = vdevice.configure(hef.value(), configure_params.value());
    if (!network_groups) {
        return make_unexpected(network_groups.status());
    }

    if (1 != network_groups->size()) {
        std::cerr << "Invalid amount of network groups" << std::endl;
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return std::move(network_groups->at(0));
}

void write_all(InputVStream &input, hailo_status &status)
{
    std::vector<uint8_t> data(input.get_frame_size());
    for (size_t i = 0; i < FRAMES_COUNT; i++) {
        status = input.write(MemoryView(data.data(), data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }
    }

    // Flushing is not mandatory here
    status = input.flush();
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed flushing input vstream" << std::endl;
        return;
    }

    status = HAILO_SUCCESS;
    return;
}

void read_all(OutputVStream &output, hailo_status &status)
{
    std::vector<uint8_t> data(output.get_frame_size());
    for (size_t i = 0; i < FRAMES_COUNT; i++) {
        status = output.read(MemoryView(data.data(), data.size()));
        if (HAILO_SUCCESS != status) {
            return;
        }
    }
    status = HAILO_SUCCESS;
    return;
}

hailo_status infer(std::vector<InputVStream> &input_streams, std::vector<OutputVStream> &output_streams)
{

    hailo_status status = HAILO_SUCCESS; // Success oriented
    hailo_status input_status[MAX_LAYER_EDGES] = {HAILO_UNINITIALIZED};
    hailo_status output_status[MAX_LAYER_EDGES] = {HAILO_UNINITIALIZED};
    std::unique_ptr<std::thread> input_threads[MAX_LAYER_EDGES];
    std::unique_ptr<std::thread> output_threads[MAX_LAYER_EDGES];
    size_t input_thread_index = 0;
    size_t output_thread_index = 0;

    // Create read threads
    for (output_thread_index = 0 ; output_thread_index < output_streams.size(); output_thread_index++) {
        output_threads[output_thread_index] = std::make_unique<std::thread>(read_all,
            std::ref(output_streams[output_thread_index]), std::ref(output_status[output_thread_index]));
    }

    // Create write threads
    for (input_thread_index = 0 ; input_thread_index < input_streams.size(); input_thread_index++) {
        input_threads[input_thread_index] = std::make_unique<std::thread>(write_all,
            std::ref(input_streams[input_thread_index]), std::ref(input_status[input_thread_index]));
    }

    // Join write threads
    for (size_t i = 0; i < input_thread_index; i++) {
        input_threads[i]->join();
        if (HAILO_SUCCESS != input_status[i]) {
            status = input_status[i];
        }
    }

    // Join read threads
    for (size_t i = 0; i < output_thread_index; i++) {
        output_threads[i]->join();
        if (HAILO_SUCCESS != output_status[i]) {
            status = output_status[i];
        }
    }

    if (HAILO_SUCCESS == status) {
        std::cout << "Inference finished successfully" << std::endl;
    }

    return status;
}

int main()
{
    auto vdevice = VDevice::create();
    if (!vdevice) {
        std::cerr << "Failed create vdevice, status = " << vdevice.status() << std::endl;
        return vdevice.status();
    }

    auto network_group = configure_network_group(*vdevice.value());
    if (!network_group) {
        std::cerr << "Failed to configure network group " << HEF_FILE << std::endl;
        return network_group.status();
    }

    // Set input format type to auto - libhailort will not scale the data before writing to the HW
    auto input_vstream_params = network_group.value()->make_input_vstream_params({}, HAILO_FORMAT_TYPE_AUTO, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!input_vstream_params) {
        std::cerr << "Failed creating input vstreams params " << input_vstream_params.status() << std::endl;
        return input_vstream_params.status();
    }

    /* The input format order in the example HEF is NHWC in the user-side (may be seen using 'hailortcli parse-hef <HEF_PATH>).
       Here we override the user-side format order to be NCHW */
    for (auto &params_pair : *input_vstream_params) {
        params_pair.second.user_buffer_format.order = HAILO_FORMAT_ORDER_NCHW;
    }

    auto input_vstreams = VStreamsBuilder::create_input_vstreams(*network_group.value(), *input_vstream_params);
    if (!input_vstreams) {
        std::cerr << "Failed creating input vstreams " << input_vstreams.status() << std::endl;
        return input_vstreams.status();
    }

    // Set output format type to float32 - libhailort will de-quantize the data after reading from the HW
    // Note: this process might affect the overall performance
    auto output_vstream_params = network_group.value()->make_output_vstream_params({}, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!output_vstream_params) {
        std::cerr << "Failed creating output vstreams params " << output_vstream_params.status() << std::endl;
        return output_vstream_params.status();
    }
    auto output_vstreams = VStreamsBuilder::create_output_vstreams(*network_group.value(), *output_vstream_params);
    if (!output_vstreams) {
        std::cerr << "Failed creating output vstreams " << output_vstreams.status() << std::endl;
        return output_vstreams.status();
    }

    if (input_vstreams->size() > MAX_LAYER_EDGES || output_vstreams->size() > MAX_LAYER_EDGES) {
        std::cerr << "Trying to infer network with too many input/output virtual streams, Maximum amount is " <<
        MAX_LAYER_EDGES << " (either change HEF or change the definition of MAX_LAYER_EDGES)"<< std::endl;
        return HAILO_INVALID_OPERATION;
    }

    auto status = infer(*input_vstreams, *output_vstreams);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Inference failed "  << status << std::endl;
        return status;
    }

    return HAILO_SUCCESS;
}
