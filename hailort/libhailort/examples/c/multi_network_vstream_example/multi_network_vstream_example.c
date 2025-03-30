/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_network_vstream_example.c
 * @brief This example demonstrates how to work with multiple networks in a network group, using virtual streams.
 **/

#include "common.h"
#include "hailo_thread.h"
#include "hailo/hailort.h"

#define INFER_FRAME_COUNT (100)
#define MAX_EDGE_LAYERS (16)
#define NET_GROUPS_COUNT (1)
#define NET_COUNT (2)
#define FIRST_NET_BATCH_SIZE (1)
#define SECOND_NET_BATCH_SIZE (2)
#define DEVICE_COUNT (1)
#define HEF_FILE ("hefs/multi_network_shortcut_net.hef")

typedef struct {
    hailo_input_vstream vstream;
    uint16_t batch_size;
} write_args_t;

typedef struct {
    hailo_output_vstream vstream;
    uint16_t batch_size;
} read_args_t;

thread_return_type write_to_device(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t input_frame_size = 0;
    uint8_t *src_data = NULL;
    write_args_t write_args = *(write_args_t*)args;
    hailo_input_vstream vstream = write_args.vstream;
    size_t batch_size = write_args.batch_size;

    status = hailo_get_input_vstream_frame_size(vstream, &input_frame_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed getting input virtual stream frame size");

    src_data = malloc(input_frame_size);
    REQUIRE_ACTION(src_data != NULL, status = HAILO_OUT_OF_HOST_MEMORY, l_exit, "Failed to allocate input buffer");

    // Prepare src data here
    for (size_t i = 0; i < input_frame_size; i++) {
        src_data[i] = (uint8_t)(rand() % 256);
    }

    const size_t network_frames_count = INFER_FRAME_COUNT * batch_size;
    for (uint32_t i = 0; i < network_frames_count; i++) {
        status = hailo_vstream_write_raw_buffer(vstream, src_data, input_frame_size);
        REQUIRE_SUCCESS(status, l_free_buffer, "Failed sending to vstream");
    }

    status = HAILO_SUCCESS;
l_free_buffer:
    FREE(src_data);
l_exit:
    return (thread_return_type)status;
}

thread_return_type read_from_device(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t output_frame_size = 0;
    uint8_t *dst_data = NULL;
    read_args_t read_args = *(read_args_t*)args;
    hailo_output_vstream vstream = read_args.vstream;
    size_t batch_size = read_args.batch_size;

    status = hailo_get_output_vstream_frame_size(vstream, &output_frame_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed getting output virtual stream frame size");

    dst_data = (uint8_t*)malloc(output_frame_size);
    REQUIRE_ACTION(dst_data != NULL, status = HAILO_OUT_OF_HOST_MEMORY, l_exit, "Failed to allocate output buffer");

    const size_t network_frames_count = INFER_FRAME_COUNT * batch_size;
    for (uint32_t i = 0; i < network_frames_count; i++) {
        status = hailo_vstream_read_raw_buffer(vstream, dst_data, output_frame_size);
        REQUIRE_SUCCESS(status, l_free_buffer, "hailo_vstream_recv failed");
    }

    status = HAILO_SUCCESS;
l_free_buffer:
    FREE(dst_data);
l_exit:
    return (thread_return_type)status;
}

hailo_status infer(hailo_input_vstream input_vstreams[NET_COUNT][MAX_EDGE_LAYERS], size_t *input_vstreams_size,
    hailo_output_vstream output_vstreams[NET_COUNT][MAX_EDGE_LAYERS], size_t *output_vstreams_size, uint16_t *batch_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_thread write_threads[NET_COUNT][MAX_EDGE_LAYERS] = {0};
    hailo_thread read_threads[NET_COUNT][MAX_EDGE_LAYERS] = {0};
    write_args_t write_args[NET_COUNT][MAX_EDGE_LAYERS] = {0};
    read_args_t read_args[NET_COUNT][MAX_EDGE_LAYERS] = {0};
    hailo_status write_thread_status = HAILO_UNINITIALIZED;
    hailo_status read_thread_status = HAILO_UNINITIALIZED;

    for (size_t net_index = 0; net_index < NET_COUNT; net_index++) {
        // Create reading threads
        for (size_t vstream_index = 0; vstream_index < output_vstreams_size[net_index]; vstream_index++) {
            read_args[net_index][vstream_index].vstream = output_vstreams[net_index][vstream_index];
            read_args[net_index][vstream_index].batch_size = batch_size[net_index];

            status = hailo_create_thread(read_from_device, &read_args[net_index][vstream_index], &read_threads[net_index][vstream_index]);
            REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");
        }

        // Create writing threads
        for (size_t vstream_index = 0; vstream_index < input_vstreams_size[net_index]; vstream_index++) {
            write_args[net_index][vstream_index].vstream = input_vstreams[net_index][vstream_index];
            write_args[net_index][vstream_index].batch_size = batch_size[net_index];

            status = hailo_create_thread(write_to_device, &write_args[net_index][vstream_index], &write_threads[net_index][vstream_index]);
            REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");
        }
    }

l_cleanup:
    // Join writing threads
    for (size_t net_index = 0; net_index < NET_COUNT; net_index++) {
        for (size_t vstream_index = 0; vstream_index < input_vstreams_size[net_index]; vstream_index++) {
            write_thread_status = hailo_join_thread(&write_threads[net_index][vstream_index]);
            if (HAILO_SUCCESS != write_thread_status) {
                printf("write_thread failed \n");
                status = write_thread_status; // Override write status
            }
        }
    }

    // Join reading threads
    for (size_t net_index = 0; net_index < NET_COUNT; net_index++) {
        for (size_t vstream_index = 0; vstream_index < output_vstreams_size[net_index]; vstream_index++) {
            read_thread_status = hailo_join_thread(&read_threads[net_index][vstream_index]);
            if (HAILO_SUCCESS != read_thread_status) {
                printf("read_thread failed \n");
                status = read_thread_status; // Override read status
            }
        }
    }

    return status;
}

int main()
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_vdevice vdevice = NULL;
    hailo_vdevice_params_t params = {0};
    hailo_hef hef = NULL;
    hailo_configure_params_t config_params = {0};
    size_t network_groups_count = NET_GROUPS_COUNT;
    size_t networks_count = NET_COUNT;
    hailo_configured_network_group network_group[NET_GROUPS_COUNT] = {NULL};
    hailo_network_info_t network_info[NET_COUNT] = {0};
    hailo_input_vstream_params_by_name_t input_vstream_params[NET_COUNT][MAX_EDGE_LAYERS] = {0};
    hailo_output_vstream_params_by_name_t output_vstream_params[NET_COUNT][MAX_EDGE_LAYERS] = {0};
    size_t input_vstreams_size[NET_COUNT] = {MAX_EDGE_LAYERS, MAX_EDGE_LAYERS};
    size_t output_vstreams_size[NET_COUNT] = {MAX_EDGE_LAYERS, MAX_EDGE_LAYERS};
    hailo_activated_network_group activated_network_group = NULL;
    hailo_input_vstream input_vstreams[NET_COUNT][MAX_EDGE_LAYERS];
    hailo_output_vstream output_vstreams[NET_COUNT][MAX_EDGE_LAYERS];
    uint16_t batch_size[NET_COUNT] = {FIRST_NET_BATCH_SIZE, SECOND_NET_BATCH_SIZE};
    bool unused = {0};

    status = hailo_init_vdevice_params(&params);
    REQUIRE_SUCCESS(status, l_exit, "Failed init vdevice_params");

    /* Scheduler does not support different batches for different networks within the same network group */
    params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
    params.device_count = DEVICE_COUNT;
    status = hailo_create_vdevice(&params, &vdevice);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create vdevice");

    status = hailo_create_hef_file(&hef, HEF_FILE);
    REQUIRE_SUCCESS(status, l_release_vdevice, "Failed reading hef file");

    status = hailo_init_configure_params_by_vdevice(hef, vdevice, &config_params);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed initializing configure parameters");

    // Modify batch_size for each network
    for (uint8_t network_index = 0; network_index < NET_COUNT; network_index++) {
        config_params.network_group_params[0].network_params_by_name[network_index].network_params.batch_size =
            batch_size[network_index];
    }

    status = hailo_configure_vdevice(vdevice, hef, &config_params, network_group, &network_groups_count);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed configure vdevcie from hef");
    REQUIRE_ACTION(NET_GROUPS_COUNT == network_groups_count, status = HAILO_INVALID_ARGUMENT, l_release_hef, 
        "Invalid network group count");

    status = hailo_get_network_infos(network_group[0], network_info, &networks_count);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed to get network groups infos");
    REQUIRE_ACTION(NET_COUNT == networks_count, status = HAILO_INVALID_ARGUMENT, l_release_hef, 
        "Invalid networks count");

    /* Build vstream params per network */
    for (uint8_t network_index = 0; network_index < NET_COUNT; network_index++) {
        status = hailo_hef_make_input_vstream_params(hef, network_info[network_index].name, unused, HAILO_FORMAT_TYPE_AUTO,
            input_vstream_params[network_index], &input_vstreams_size[network_index]);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed making input virtual stream params");

        status = hailo_hef_make_output_vstream_params(hef, network_info[network_index].name, unused, HAILO_FORMAT_TYPE_AUTO,
            output_vstream_params[network_index], &output_vstreams_size[network_index]);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed making output virtual stream params");

        REQUIRE_ACTION((input_vstreams_size[network_index] <= MAX_EDGE_LAYERS ||
            output_vstreams_size[network_index] <= MAX_EDGE_LAYERS), status = HAILO_INVALID_OPERATION, l_release_hef,
            "Trying to infer network with too many input/output virtual streams, "
            "Maximum amount is %d, (either change HEF or change the definition of MAX_EDGE_LAYERS)\n",
            MAX_EDGE_LAYERS);
    }

    /* Build vstreams per network */
    for (uint8_t network_index = 0; network_index < NET_COUNT; network_index++) {
        status = hailo_create_input_vstreams(network_group[0], input_vstream_params[network_index],
            input_vstreams_size[network_index], input_vstreams[network_index]);
        REQUIRE_SUCCESS(status, l_release_vstreams, "Failed creating input virtual streams\n");

        status = hailo_create_output_vstreams(network_group[0], output_vstream_params[network_index],
            output_vstreams_size[network_index], output_vstreams[network_index]);
        REQUIRE_SUCCESS(status, l_release_vstreams, "Failed creating output virtual streams\n");
    }

    status = hailo_activate_network_group(network_group[0], NULL, &activated_network_group);
    REQUIRE_SUCCESS(status, l_release_vstreams, "Failed activate network group");

    status = infer(input_vstreams, input_vstreams_size, output_vstreams, output_vstreams_size, batch_size);
    REQUIRE_SUCCESS(status, l_deactivate_network_group, "Inference failure");

    printf("Inference ran successfully\n");
    status = HAILO_SUCCESS;

l_deactivate_network_group:
    (void)hailo_deactivate_network_group(activated_network_group);
l_release_vstreams:
    for (size_t net_index = 0; net_index < NET_COUNT; net_index++) {
        if (NULL != output_vstreams[net_index][0]) {
            (void)hailo_release_output_vstreams(output_vstreams[net_index], output_vstreams_size[net_index]);
        }
    }
    for (size_t net_index = 0; net_index < NET_COUNT; net_index++) {
        if (NULL != input_vstreams[net_index][0]) {
            (void)hailo_release_input_vstreams(input_vstreams[net_index], input_vstreams_size[net_index]);
        }
    }
l_release_hef:
    (void) hailo_release_hef(hef);
l_release_vdevice:
    (void) hailo_release_vdevice(vdevice);
l_exit:
    return (int)status;
}
