/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_device_example.c
 * This example demonstrates how to work with multiple devices using virtual device.
 * The program scans for Hailo-8 devices connected to a provided PCIe interface, generates random dataset,
 * and runs it through the virtual device with virtual streams.
 **/

#include "common.h"
#include "hailo_thread.h"
#include "hailo/hailort.h"

#define INFER_FRAME_COUNT (100)
#define MAX_EDGE_LAYERS (16)
#define MAX_DEVICES (16)
#define BATCH_SIZE (1)
#define HEF_FILE ("hefs/shortcut_net.hef")


thread_return_type write_to_vdevice(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t input_frame_size = 0;
    uint8_t *src_data = NULL;
    hailo_input_vstream vstream = *(hailo_input_vstream*)args;

    status = hailo_get_input_vstream_frame_size(vstream, &input_frame_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed getting input virtual stream frame size");

    src_data = malloc(input_frame_size);
    REQUIRE_ACTION(src_data != NULL, status = HAILO_OUT_OF_HOST_MEMORY, l_exit, "Failed to allocate input buffer");

    // Prepare src data here
    for (size_t i = 0; i < input_frame_size; i++) {
        src_data[i] = (uint8_t)(rand() % 256);
    }

    for (uint32_t i = 0; i < INFER_FRAME_COUNT; i++) {
        status = hailo_vstream_write_raw_buffer(vstream, src_data, input_frame_size);
        REQUIRE_SUCCESS(status, l_free_buffer, "Failed sending to vstream");
    }

    // Flushing is not mandatory here
    status = hailo_flush_input_vstream(vstream);
    REQUIRE_SUCCESS(status, l_free_buffer, "Failed flushing vstream");

    status = HAILO_SUCCESS;
l_free_buffer:
    FREE(src_data);
l_exit:
    return (thread_return_type)status;
}

thread_return_type read_from_vdevice(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t output_frame_size = 0;
    uint8_t *dst_data = NULL;
    hailo_output_vstream vstream = *(hailo_output_vstream*)args;

    status = hailo_get_output_vstream_frame_size(vstream, &output_frame_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed getting output virtual stream frame size");

    dst_data = (uint8_t*)malloc(output_frame_size);
    REQUIRE_ACTION(dst_data != NULL, status = HAILO_OUT_OF_HOST_MEMORY, l_exit, "Failed to allocate output buffer");

    for (uint32_t i = 0; i < INFER_FRAME_COUNT; i++) {
        status = hailo_vstream_read_raw_buffer(vstream, dst_data, output_frame_size);
        REQUIRE_SUCCESS(status, l_free_buffer, "hailo_vstream_recv failed");
    }

    status = HAILO_SUCCESS;
l_free_buffer:
    FREE(dst_data);
l_exit:
    return (thread_return_type)status;
}

hailo_status infer(hailo_input_vstream *input_vstreams, size_t input_vstreams_size,
    hailo_output_vstream *output_vstreams, size_t output_vstreams_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_thread write_threads[MAX_EDGE_LAYERS] = {0};
    hailo_thread read_threads[MAX_EDGE_LAYERS] = {0};
    hailo_status write_thread_status = HAILO_UNINITIALIZED;
    hailo_status read_thread_status = HAILO_UNINITIALIZED;
    size_t input_threads_index = 0;
    size_t output_threads_index = 0;
    size_t index = 0;

    // Create reading threads
    for (output_threads_index = 0; output_threads_index < output_vstreams_size; output_threads_index++) {
        status = hailo_create_thread(read_from_vdevice, &output_vstreams[output_threads_index], &read_threads[output_threads_index]);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");
    }

    // Create writing threads
    for (input_threads_index = 0; input_threads_index < input_vstreams_size; input_threads_index++) {
        status = hailo_create_thread(write_to_vdevice, &input_vstreams[input_threads_index], &write_threads[input_threads_index]);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");
    }

l_cleanup:
    // Join writing threads
    for (index = 0; index < input_threads_index; index++) {
        write_thread_status = hailo_join_thread(&write_threads[index]);
        if (HAILO_SUCCESS != write_thread_status) {
            printf("write_thread failed \n");
            status = write_thread_status; // Override write status
        }
    }

    // Join reading threads
    for (index = 0; index < output_threads_index; index++) {
        read_thread_status = hailo_join_thread(&read_threads[index]);
        if (HAILO_SUCCESS != read_thread_status) {
            printf("read_thread failed \n");
            status = read_thread_status; // Override read status
        }
    }

    return status;
}

int main()
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_vdevice vdevice = NULL;
    hailo_device_id_t device_ids[MAX_DEVICES];
    size_t actual_count = MAX_DEVICES;
    hailo_vdevice_params_t params = {0};
    hailo_hef hef = NULL;
    hailo_configure_params_t config_params = {0};
    uint16_t batch_size = BATCH_SIZE;
    hailo_configured_network_group network_group = NULL;
    size_t network_group_size = 1;
    hailo_input_vstream_params_by_name_t input_vstream_params[MAX_EDGE_LAYERS] = {0};
    hailo_output_vstream_params_by_name_t output_vstream_params[MAX_EDGE_LAYERS] = {0};
    size_t input_vstreams_size = MAX_EDGE_LAYERS;
    size_t output_vstreams_size = MAX_EDGE_LAYERS;
    hailo_input_vstream input_vstreams[MAX_EDGE_LAYERS] = {NULL};
    hailo_output_vstream output_vstreams[MAX_EDGE_LAYERS] = {NULL};
    bool unused = {0};

    status = hailo_scan_devices(NULL, device_ids, &actual_count);
    REQUIRE_SUCCESS(status, l_exit, "Failed to scan devices");
    printf("Found %zu devices\n", actual_count);

    status = hailo_init_vdevice_params(&params);
    REQUIRE_SUCCESS(status, l_exit, "Failed init vdevice_params");

    params.device_count = (uint32_t)actual_count;
    status = hailo_create_vdevice(&params, &vdevice);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create vdevice");

    status = hailo_create_hef_file(&hef, HEF_FILE);
    REQUIRE_SUCCESS(status, l_release_vdevice, "Failed reading hef file");

    status = hailo_init_configure_params_by_vdevice(hef, vdevice, &config_params);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed initializing configure parameters");

    // Modify batch_size and power_mode for each network group
    for (size_t i = 0; i < config_params.network_group_params_count; i++) {
        config_params.network_group_params[i].batch_size = batch_size;
        config_params.network_group_params[i].power_mode = HAILO_POWER_MODE_ULTRA_PERFORMANCE;
    }

    status = hailo_configure_vdevice(vdevice, hef, &config_params, &network_group, &network_group_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed configure vdevcie from hef");
    REQUIRE_ACTION(network_group_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef, 
        "Invalid network group size");

    status = hailo_make_input_vstream_params(network_group, unused, HAILO_FORMAT_TYPE_AUTO,
        input_vstream_params, &input_vstreams_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed making input virtual stream params");

    status = hailo_make_output_vstream_params(network_group, unused, HAILO_FORMAT_TYPE_AUTO,
        output_vstream_params, &output_vstreams_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed making output virtual stream params");

    REQUIRE_ACTION((input_vstreams_size <= MAX_EDGE_LAYERS || output_vstreams_size <= MAX_EDGE_LAYERS),
        status = HAILO_INVALID_OPERATION, l_release_hef, "Trying to infer network with too many input/output virtual "
        "streams, Maximum amount is %d, (either change HEF or change the definition of MAX_EDGE_LAYERS)\n",
        MAX_EDGE_LAYERS);

    status = hailo_create_input_vstreams(network_group, input_vstream_params, input_vstreams_size, input_vstreams);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed creating virtual input streams\n");

    status = hailo_create_output_vstreams(network_group, output_vstream_params, output_vstreams_size, output_vstreams);
    REQUIRE_SUCCESS(status, l_release_input_vstream, "Failed creating output virtual streams\n");

    status = infer(input_vstreams, input_vstreams_size, output_vstreams, output_vstreams_size);
    REQUIRE_SUCCESS(status, l_release_output_vstream, "Inference failure");

    printf("Inference ran successfully\n");
    status = HAILO_SUCCESS;
l_release_output_vstream:
    (void)hailo_release_output_vstreams(output_vstreams, output_vstreams_size);
l_release_input_vstream:
    (void)hailo_release_input_vstreams(input_vstreams, input_vstreams_size);
l_release_hef:
    (void) hailo_release_hef(hef);
l_release_vdevice:
    (void) hailo_release_vdevice(vdevice);
l_exit:
    return (int)status;
}
