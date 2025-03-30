/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file switch_network_groups_manually_example.c
 * This example demonstrates basic usage of HailoRT streaming api over multiple network groups, using vstreams.
 * It loads several HEF networks with a single input and a single output into a Hailo VDevice and performs a inference on each one.
 * After inference is finished, the example switches to the next HEF and start inference again.
 **/

#include "common.h"
#include "hailo_thread.h"
#include "hailo/hailort.h"
#include <time.h>

#define MAX_HEF_PATH_LEN (255)
#define MAX_EDGE_LAYERS (16)

#define INFER_FRAME_COUNT (100)
#define HEF_COUNT (2)
#define RUN_COUNT (10)
#define DEVICE_COUNT (1)

typedef struct input_vstream_thread_args_t {
    hailo_configured_network_group *configured_networks;
    hailo_input_vstream_params_by_name_t *input_vstream_params;
} input_vstream_thread_args_t;

typedef struct output_vstream_thread_args_t {
    hailo_configured_network_group *configured_networks;
    hailo_activated_network_group *activated_network_group;
    hailo_output_vstream_params_by_name_t *output_vstream_params;
} output_vstream_thread_args_t;

thread_return_type input_vstream_thread_func(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_input_vstream input_vstreams[HEF_COUNT];
    size_t input_frame_size[HEF_COUNT];
    uint8_t *src_data[HEF_COUNT];
    input_vstream_thread_args_t *input_vstream_args = (input_vstream_thread_args_t*)args;

    for (size_t hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        // Input vstream size has to be one in this example- otherwise would haveve returned error
        status = hailo_create_input_vstreams(input_vstream_args->configured_networks[hef_index],
            &input_vstream_args->input_vstream_params[hef_index], 1, &input_vstreams[hef_index]);
        REQUIRE_SUCCESS(status, l_clear_src, "Failed creating virtual stream");

        status = hailo_get_input_vstream_frame_size(input_vstreams[hef_index], &input_frame_size[hef_index]);
        REQUIRE_SUCCESS(status, l_clear_src, "Failed getting input virtual stream frame size");

        src_data[hef_index] = (uint8_t*)malloc(input_frame_size[hef_index]);
        REQUIRE_ACTION(NULL != src_data[hef_index], status = HAILO_OUT_OF_HOST_MEMORY, l_clear_src, "Out of memory");
    }

    for (size_t run_index = 0; run_index < RUN_COUNT; run_index++) {
        for (size_t hef_index = 0 ; hef_index < HEF_COUNT; hef_index++) {
            // Wait for hef to be activated to send data
            status = hailo_wait_for_network_group_activation(input_vstream_args->configured_networks[hef_index], HAILO_INFINITE);
            REQUIRE_SUCCESS(status, l_clear_src, "Failed waiting for network group activation");

            // Send data on relevant Hef
            for (uint32_t frame = 0; frame < INFER_FRAME_COUNT; frame++) {
                // Prepare data here
                for (size_t i = 0; i < input_frame_size[hef_index]; i++) {
                    src_data[hef_index][i] = (uint8_t)(rand() % 256);
                }

                status = hailo_vstream_write_raw_buffer(input_vstreams[hef_index], src_data[hef_index], input_frame_size[hef_index]);
                REQUIRE_SUCCESS(status, l_clear_src, "Failed writing input frame to device");
            }
        }
    }

    status = HAILO_SUCCESS;

l_clear_src:
    for (size_t hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        FREE(src_data[hef_index]);
    }

    (void)hailo_release_input_vstreams(input_vstreams, HEF_COUNT);
    return (thread_return_type)status;
}

thread_return_type output_vstream_thread_func(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_output_vstream output_vstreams[HEF_COUNT];
    size_t output_frame_size[HEF_COUNT];
    uint8_t *dst_data[HEF_COUNT];
    output_vstream_thread_args_t *output_vstream_args = (output_vstream_thread_args_t*)args;

    for (size_t hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        // Output vstream size has to be one in this example- otherwise would haveve returned error
        status = hailo_create_output_vstreams(output_vstream_args->configured_networks[hef_index],
            &output_vstream_args->output_vstream_params[hef_index], 1, &output_vstreams[hef_index]);
        REQUIRE_SUCCESS(status, l_clear_dst, "Failed creating virtual stream");

        status = hailo_get_output_vstream_frame_size(output_vstreams[hef_index], &output_frame_size[hef_index]);
        REQUIRE_SUCCESS(status, l_clear_dst, "Failed getting input virtual stream frame size");

        dst_data[hef_index] = (uint8_t*)malloc(output_frame_size[hef_index]);
        REQUIRE_ACTION(NULL != dst_data[hef_index], status = HAILO_OUT_OF_HOST_MEMORY, l_clear_dst, "Out of memory");
    }

    for (size_t run_index = 0; run_index < RUN_COUNT; run_index++) {
        for (size_t hef_index = 0 ; hef_index < HEF_COUNT; hef_index++) {
            // Wait for hef to be activated to recv data
            status = hailo_wait_for_network_group_activation(output_vstream_args->configured_networks[hef_index], HAILO_INFINITE);
            REQUIRE_SUCCESS(status, l_clear_dst, "Failed waiting for network group activation");

            for (uint32_t i = 0; i < INFER_FRAME_COUNT; i++) {
                // Read data
                status = hailo_vstream_read_raw_buffer(output_vstreams[hef_index],
                    dst_data[hef_index], output_frame_size[hef_index]);
                REQUIRE_SUCCESS(status, l_deactivate_network_group, "Failed reading output frame from device");

                // Process data here
            }

            // Deavticate network after finishing inference
            status = hailo_deactivate_network_group(*(output_vstream_args->activated_network_group));
            REQUIRE_SUCCESS(status, l_deactivate_network_group, "Failed Deactivating network");

            // Dont activate on last iteration
            if (hef_index < HEF_COUNT - 1) {
                // Activate next network so input thread can start sending again
                status = hailo_activate_network_group(output_vstream_args->configured_networks[hef_index + 1],
                    NULL, output_vstream_args->activated_network_group);
                REQUIRE_SUCCESS(status, l_clear_dst, "Failed Activating network");
            }
            else {
                // Meaning we finished a run and now need to activate the first network again for the next run
                if (run_index < RUN_COUNT - 1) {
                    status = hailo_activate_network_group(output_vstream_args->configured_networks[0],
                        NULL, output_vstream_args->activated_network_group);
                    REQUIRE_SUCCESS(status, l_clear_dst, "Failed Activating network");
                }
            }
        }
    }

    status = HAILO_SUCCESS;
    goto l_clear_dst;

l_deactivate_network_group:
    (void)hailo_deactivate_network_group(*(output_vstream_args->activated_network_group));
l_clear_dst:
    for (size_t hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        FREE(dst_data[hef_index]);
    }

    (void)hailo_release_output_vstreams(output_vstreams, HEF_COUNT);
    return (thread_return_type)status;
}

int main()
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_vdevice vdevice = NULL;
    hailo_vdevice_params_t params = {0};
    hailo_hef hef[HEF_COUNT] = {NULL};
    hailo_configure_params_t configure_params = {0};
    hailo_activated_network_group activated_network;
    hailo_configured_network_group network_groups[HEF_COUNT] = {NULL};
    size_t network_groups_size = 1;
    uint8_t hef_index = 0;
    hailo_input_vstream_params_by_name_t input_vstream_params[HEF_COUNT];
    hailo_output_vstream_params_by_name_t output_vstream_params[HEF_COUNT];
    size_t input_vstream_size = 1;
    size_t output_vstream_size = 1;

    hailo_thread input_vstream_thread = {0};
    hailo_thread output_vstream_thread = {0};
    input_vstream_thread_args_t input_args = {0};
    output_vstream_thread_args_t output_args = {0};
    bool unused = {0};

    char HEF_FILES[HEF_COUNT][250] = {"hefs/shortcut_net.hef","hefs/shortcut_net.hef"};

    status = hailo_init_vdevice_params(&params);
    REQUIRE_SUCCESS(status, l_exit, "Failed init vdevice_params");

    params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
    params.device_count = DEVICE_COUNT;
    status = hailo_create_vdevice(&params, &vdevice);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create vdevice");

    for (hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        /* Select user HEFs here. In this example it's the same HEF for all networks */
        status = hailo_create_hef_file(&hef[hef_index], HEF_FILES[hef_index]);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed creating hef file %s", HEF_FILES[hef_index]);

        status = hailo_init_configure_params_by_vdevice(hef[hef_index], vdevice, &configure_params);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed init configure params");

        status = hailo_configure_vdevice(vdevice, hef[hef_index], &configure_params, &network_groups[hef_index], &network_groups_size);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed configuring vdevcie");
        REQUIRE_ACTION(network_groups_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef,
            "Unexpected network group size");

        // Mae sure each hef is single input single output
        status = hailo_make_input_vstream_params(network_groups[hef_index], unused, HAILO_FORMAT_TYPE_AUTO,
            &input_vstream_params[hef_index], &input_vstream_size);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed making input virtual stream params");
        REQUIRE_ACTION(input_vstream_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef,
            "INVALID HEF - Only hefs with single input vstream are allowed");

        status = hailo_make_output_vstream_params(network_groups[hef_index], unused, HAILO_FORMAT_TYPE_AUTO,
            &output_vstream_params[hef_index], &output_vstream_size);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed making output virtual stream params");
        REQUIRE_ACTION(output_vstream_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef,
            "INVALID HEF - Only hefs with single output vstream are allowed");
    }

    input_args.configured_networks      = network_groups;
    input_args.input_vstream_params     = input_vstream_params;

    // Open input vstream and output vstream threads
    status = hailo_create_thread(input_vstream_thread_func, &input_args, &input_vstream_thread);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed creating thread");

    output_args.configured_networks         = network_groups;
    output_args.activated_network_group     = &activated_network;
    output_args.output_vstream_params       = output_vstream_params;

    // Open output vstream and output vstream threads
    status = hailo_create_thread(output_vstream_thread_func, &output_args, &output_vstream_thread);
    REQUIRE_SUCCESS(status, l_join_input_thread, "Failed creating thread");

    // Activate first network so input thread can start
    status = hailo_activate_network_group(network_groups[0], NULL, &activated_network);
    REQUIRE_SUCCESS(status, l_join_output_thread, "Failed Activating network");


    status = hailo_join_thread(&input_vstream_thread);
    REQUIRE_SUCCESS(status, l_join_output_thread, "Failed witing for input thread");

    status = hailo_join_thread(&output_vstream_thread);
    REQUIRE_SUCCESS(status, l_join_output_thread, "Failed witing for output thread");

    printf("Inference ran successfully\n");
    status = HAILO_SUCCESS;
    goto l_release_hef;

l_join_output_thread:
    (void)hailo_join_thread(&output_vstream_thread);
l_join_input_thread:
    (void)hailo_join_thread(&input_vstream_thread);
l_release_hef:
    for (hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        if (NULL != hef[hef_index]) {
            (void)hailo_release_hef(hef[hef_index]);
        }
    }
    (void)hailo_release_vdevice(vdevice);
l_exit:
    return (int)status;
}
