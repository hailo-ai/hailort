/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file raw_streams_example.c
 * This example demonstrates basic usage of HailoRT streaming api.
 * It loads an HEF network with multiple inputs and multiple outputs into a Hailo device and performs a
 * short inference.
 **/

#include "common.h"
#include "hailo_thread.h"
#include "hailo/hailort.h"

#define HEF_FILE ("hefs/shortcut_net.hef")
#define INFER_FRAME_COUNT (200)
#define MAX_EDGE_LAYERS (16)
#define DEVICE_IDS_COUNT (16)

typedef struct write_thread_args_t {
    hailo_input_stream *input_stream;
    hailo_stream_info_t *input_stream_info;
} write_thread_args_t;

typedef struct read_thread_args_t {
    hailo_output_stream *output_stream;
    hailo_stream_info_t *output_stream_info;
} read_thread_args_t;

thread_return_type write_to_device(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_transform_params_t transform_params = HAILO_DEFAULT_TRANSFORM_PARAMS;
    hailo_input_transform_context transform_context = NULL;
    uint8_t *hw_data = NULL;
    uint8_t *host_data = NULL;
    size_t host_frame_size = 0;
    write_thread_args_t *write_args = (write_thread_args_t*)args; 

    status = hailo_create_input_transform_context(write_args->input_stream_info, &transform_params, &transform_context);
    REQUIRE_SUCCESS(status, l_exit, "Failed creating input transform_context");

    hw_data = (uint8_t*)malloc(write_args->input_stream_info->hw_frame_size);
    host_frame_size = hailo_get_host_frame_size(write_args->input_stream_info, &transform_params);
    host_data = (uint8_t*)malloc(host_frame_size);
    REQUIRE_ACTION((NULL != hw_data) && (NULL != host_data), status = HAILO_OUT_OF_HOST_MEMORY,
        l_cleanup, "Out of memory");

    // Prepare src data here
    for (size_t i = 0; i < host_frame_size; i++) {
        host_data[i] = (uint8_t)(rand() % 256);
    }

    for (uint32_t i = 0; i < INFER_FRAME_COUNT; i++) {
        // Transform + Write
        status = hailo_transform_frame_by_input_transform_context(transform_context, host_data, host_frame_size,
            hw_data, write_args->input_stream_info->hw_frame_size);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed transforming input frame");

        status = hailo_stream_write_raw_buffer(*(write_args->input_stream), hw_data,
            write_args->input_stream_info->hw_frame_size);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed writing input frame to device");
    }

l_cleanup:
    FREE(host_data);
    FREE(hw_data);
    hailo_release_input_transform_context(transform_context);
        
l_exit:
    return (thread_return_type)status;
}

thread_return_type read_from_device(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_transform_params_t transform_params = HAILO_DEFAULT_TRANSFORM_PARAMS;
    hailo_output_transform_context transform_context = NULL;
    uint8_t *hw_data = NULL;
    uint8_t *host_data = NULL;
    size_t host_frame_size = 0;
    read_thread_args_t *read_args = (read_thread_args_t*)args; 

    status = hailo_create_output_transform_context(read_args->output_stream_info, &transform_params, &transform_context);
    REQUIRE_SUCCESS(status, l_exit, "Failed creating output transform_context");

    hw_data = (uint8_t*)malloc(read_args->output_stream_info->hw_frame_size);
    host_frame_size = hailo_get_host_frame_size(read_args->output_stream_info, &transform_params);
    host_data = (uint8_t*)malloc(host_frame_size);
    REQUIRE_ACTION((NULL != hw_data) && (NULL != host_data), status = HAILO_OUT_OF_HOST_MEMORY,
        l_cleanup, "Out of memory");

    for (uint32_t i = 0; i < INFER_FRAME_COUNT; i++) {
        // Read + Transform
        status = hailo_stream_read_raw_buffer(*(read_args->output_stream), hw_data,
            read_args->output_stream_info->hw_frame_size);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed reading output frame from device");

        status = hailo_transform_frame_by_output_transform_context(transform_context, hw_data,
            read_args->output_stream_info->hw_frame_size, host_data, host_frame_size);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed transforming output frame");

        // Process data here
    }

l_cleanup:
    FREE(host_data);
    FREE(hw_data);
    hailo_release_output_transform_context(transform_context);
l_exit:
    return (thread_return_type)status;
}

hailo_status infer(hailo_input_stream *input_streams, hailo_stream_info_t *input_streams_info,
    size_t number_input_streams,hailo_output_stream *output_streams, hailo_stream_info_t *output_streams_info, 
    size_t number_output_streams)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_thread write_threads [MAX_EDGE_LAYERS] = {0};
    hailo_thread read_threads [MAX_EDGE_LAYERS] = {0};
    write_thread_args_t write_thread_args [MAX_EDGE_LAYERS] = {0};
    read_thread_args_t read_thread_args [MAX_EDGE_LAYERS] = {0};
    hailo_status write_thread_status = HAILO_UNINITIALIZED;
    hailo_status read_thread_status = HAILO_UNINITIALIZED;
    size_t input_threads_index = 0;
    size_t output_threads_index = 0;
    size_t index = 0;

    // Run read threads
    for (output_threads_index = 0; output_threads_index < number_output_streams; output_threads_index++) {
        read_thread_args[output_threads_index].output_stream = &output_streams[output_threads_index];
        read_thread_args[output_threads_index].output_stream_info = &output_streams_info[output_threads_index];

        status = hailo_create_thread(read_from_device, &read_thread_args[output_threads_index],
            &read_threads[output_threads_index]);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");

    }

    // Run write threads
    for (input_threads_index = 0; input_threads_index < number_input_streams; input_threads_index++) {
        write_thread_args[input_threads_index].input_stream = &input_streams[input_threads_index];
        write_thread_args[input_threads_index].input_stream_info = &input_streams_info[input_threads_index];

        status = hailo_create_thread(write_to_device, &write_thread_args[input_threads_index],
            &write_threads[input_threads_index]);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");
    }

l_cleanup:
    // Join write threads
    for (index = 0; index < input_threads_index; index++) {
        write_thread_status = hailo_join_thread(&write_threads[index]);
        if (HAILO_SUCCESS != write_thread_status) {
            status = write_thread_status; // Override write status
        }
    }

    // Join read threads
    for (index = 0; index < output_threads_index; index++) {
        read_thread_status = hailo_join_thread(&read_threads[index]);
        if (HAILO_SUCCESS != read_thread_status) {
            status = read_thread_status; // Override read status
        }
    }

    return status;
}

int main()
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_device_id_t device_ids[DEVICE_IDS_COUNT];
    size_t actual_devices_count = DEVICE_IDS_COUNT;
    hailo_device device = NULL;
    hailo_hef hef = NULL;
    hailo_configure_params_t configure_params = {0};
    hailo_configured_network_group network_group = NULL;
    size_t network_group_size = 1;
    hailo_activated_network_group activated_network_group = NULL;
    hailo_stream_info_t input_streams_info [MAX_EDGE_LAYERS] = {0};
    hailo_stream_info_t output_streams_info [MAX_EDGE_LAYERS] = {0};
    hailo_input_stream input_streams [MAX_EDGE_LAYERS] = {NULL};
    hailo_output_stream output_streams [MAX_EDGE_LAYERS] = {NULL};
    size_t number_input_streams = 0;
    size_t number_output_streams = 0;
    size_t i = 0;

    status = hailo_scan_devices(NULL, device_ids, &actual_devices_count);
    REQUIRE_SUCCESS(status, l_exit, "Failed to scan devices");
    REQUIRE_ACTION(1 <= actual_devices_count, status = HAILO_INVALID_OPERATION, l_exit,
        "Failed to find a connected hailo device.");

    status = hailo_create_device_by_id(&(device_ids[0]), &device);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create device");

    status = hailo_create_hef_file(&hef, HEF_FILE);
    REQUIRE_SUCCESS(status, l_release_device, "Failed creating hef file %s", HEF_FILE);

    status = hailo_init_configure_params_by_device(hef, device, &configure_params);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed init configure params");

    status = hailo_configure_device(device, hef, &configure_params, &network_group, &network_group_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed configuring devcie");
    REQUIRE_ACTION(network_group_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef, 
        "Unexpected network group size");

    status = hailo_network_group_get_input_stream_infos(network_group, input_streams_info, MAX_EDGE_LAYERS,
        &number_input_streams);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed getting input streams infos");

    status = hailo_network_group_get_output_stream_infos(network_group, output_streams_info, MAX_EDGE_LAYERS,
        &number_output_streams);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed getting output streams infos");

    for (i = 0; i < number_input_streams; i++) {
        status = hailo_get_input_stream(network_group, input_streams_info[i].name, &input_streams[i]);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed getting input stream %s", input_streams_info[i].name);
    }

    for (i = 0; i < number_output_streams; i++) {
        status = hailo_get_output_stream(network_group, output_streams_info[i].name, &output_streams[i]);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed getting output stream %s", output_streams_info[i].name);
    }

    status = hailo_activate_network_group(network_group, NULL, &activated_network_group);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed activate network group");

    status = infer(input_streams, input_streams_info, number_input_streams, output_streams, output_streams_info,
        number_output_streams);
    REQUIRE_SUCCESS(status, l_deactivate_network_group, "Failed performing inference");
    printf("Inference ran successfully\n");

l_deactivate_network_group:
    (void)hailo_deactivate_network_group(activated_network_group);
l_release_hef:
    (void) hailo_release_hef(hef);
l_release_device:
    (void) hailo_release_device(device);
l_exit:
    return (int)status;
}
