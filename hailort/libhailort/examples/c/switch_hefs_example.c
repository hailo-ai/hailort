/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @ file switch_hefs_example.c
 * This example demonstrates basic usage of HailoRT streaming api over multiple network groups, using vstreams.
 * It loads several HEF networks with single/multiple inputs and single/multiple outputs into a Hailo PCIe VDevice and performs a
 * short inference on each one. 
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

typedef struct write_thread_args_t {
    hailo_input_vstream input_vstream;
    uint8_t *src_data;
    size_t src_frame_size;
} write_thread_args_t;

typedef struct read_thread_args_t {
    hailo_output_vstream output_vstream;
    uint8_t *dst_data;
    size_t dst_frame_size;
} read_thread_args_t;

thread_return_type write_to_device(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    write_thread_args_t *write_args = (write_thread_args_t*)args;

    for (uint32_t frame = 0; frame < INFER_FRAME_COUNT; frame++) {
        // Write data
        status = hailo_vstream_write_raw_buffer(write_args->input_vstream, write_args->src_data, write_args->src_frame_size);
        REQUIRE_SUCCESS(status, l_exit, "Failed writing input frame to device");
    }

    status = HAILO_SUCCESS;
l_exit:
    return (thread_return_type)status;
}

thread_return_type read_from_device(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    read_thread_args_t *read_args = (read_thread_args_t*)args;

    for (uint32_t i = 0; i < INFER_FRAME_COUNT; i++) {
        // Read data
        status = hailo_vstream_read_raw_buffer(read_args->output_vstream, read_args->dst_data, read_args->dst_frame_size);
        REQUIRE_SUCCESS(status, l_exit, "Failed reading output frame from device");

        // Process data here
    }

    status = HAILO_SUCCESS;
l_exit:
    return (thread_return_type)status;
}

hailo_status create_input_vstream_thread(hailo_input_vstream input_vstream, uint8_t *src_data, size_t src_frame_size,
    hailo_thread *input_thread, write_thread_args_t *write_args)
{
    write_args->src_data = src_data;
    write_args->src_frame_size = src_frame_size;
    write_args->input_vstream = input_vstream;

    // Run write
    return hailo_create_thread(write_to_device, write_args, input_thread);
}

hailo_status create_output_vstream_thread(hailo_output_vstream output_vstream, uint8_t *dst_data, size_t dst_frame_size,
    hailo_thread *output_thread, read_thread_args_t *read_args)
{
    read_args->dst_data = dst_data;
    read_args->dst_frame_size = dst_frame_size;
    read_args->output_vstream = output_vstream;

    // Run read
    return hailo_create_thread(read_from_device, read_args, output_thread);
}

hailo_status build_vstreams(hailo_configured_network_group network_group,
    hailo_input_vstream *input_vstreams, size_t *input_frame_sizes, uint8_t **src_data,
    hailo_output_vstream *output_vstreams, size_t *output_frame_sizes, uint8_t **dst_data,
    size_t *num_input_vstreams, size_t *num_output_vstreams)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_input_vstream_params_by_name_t input_vstream_params[MAX_EDGE_LAYERS];
    hailo_output_vstream_params_by_name_t output_vstream_params[MAX_EDGE_LAYERS];

    // Make sure it can hold amount of vstreams for hailo_make_input/output_vstream_params
    size_t input_vstream_size = MAX_EDGE_LAYERS;
    size_t output_vstream_size = MAX_EDGE_LAYERS;

    status = hailo_make_input_vstream_params(network_group, true, HAILO_FORMAT_TYPE_AUTO,
        input_vstream_params, &input_vstream_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed making input virtual stream params");
    *num_input_vstreams = input_vstream_size;

    status = hailo_make_output_vstream_params(network_group, true, HAILO_FORMAT_TYPE_AUTO,
        output_vstream_params, &output_vstream_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed making output virtual stream params");
    *num_output_vstreams = output_vstream_size;

    REQUIRE_ACTION((*num_input_vstreams <= MAX_EDGE_LAYERS || *num_output_vstreams <= MAX_EDGE_LAYERS),
        status = HAILO_INVALID_OPERATION, l_exit, "Trying to infer network with too many input/output virtual streams, "
        "Maximum amount is %d, (either change HEF or change the definition of MAX_EDGE_LAYERS)\n", MAX_EDGE_LAYERS);

    status = hailo_create_input_vstreams(network_group, input_vstream_params, input_vstream_size, input_vstreams);
    REQUIRE_SUCCESS(status, l_exit, "Failed creating virtual stream");

    status = hailo_create_output_vstreams(network_group, output_vstream_params, output_vstream_size, output_vstreams);
    REQUIRE_SUCCESS(status, l_release_input_vstream, "Failed creating virtual stream");

    for (size_t i = 0; i < input_vstream_size; i++) {
        status = hailo_get_input_vstream_frame_size(input_vstreams[i], &input_frame_sizes[i]);
        REQUIRE_SUCCESS(status, l_clear_buffers, "Failed getting input virtual stream frame size");

        src_data[i] = (uint8_t*)malloc(input_frame_sizes[i]);
        REQUIRE_ACTION(NULL != src_data[i], status = HAILO_OUT_OF_HOST_MEMORY, l_clear_buffers, "Out of memory");

        // Prepare data here
        for (size_t frame_index = 0; frame_index < input_frame_sizes[i]; frame_index++) {
            src_data[i][frame_index] = (uint8_t)(rand() % 256);
        }
    }  

    for (size_t i = 0; i < output_vstream_size; i++) {
        status = hailo_get_output_vstream_frame_size(output_vstreams[i], &output_frame_sizes[i]);
        REQUIRE_SUCCESS(status, l_clear_buffers, "Failed getting input virtual stream frame size");

        dst_data[i] = (uint8_t*)malloc(output_frame_sizes[i]);
        REQUIRE_ACTION(NULL != dst_data[i], status = HAILO_OUT_OF_HOST_MEMORY, l_clear_buffers, "Out of memory");
    }

    status = HAILO_SUCCESS;
    goto l_exit;

l_clear_buffers:
    for (size_t i = 0; i < input_vstream_size; i++) {
        FREE(src_data[i]);
    }
    for (size_t i = 0; i < output_vstream_size; i++) {
        FREE(dst_data[i]);
    }

    (void)hailo_release_output_vstreams(output_vstreams, output_vstream_size);
l_release_input_vstream:
    (void)hailo_release_input_vstreams(input_vstreams, input_vstream_size);
l_exit:
    return status;
}

int main()
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_vdevice vdevice = NULL;
    hailo_vdevice_params_t params = {0};
    hailo_hef hef[HEF_COUNT] = {NULL};
    hailo_configure_params_t configure_params = {0};
    hailo_configured_network_group network_groups[HEF_COUNT] = {NULL};
    size_t network_groups_size = 1;
    hailo_activated_network_group activated_network_group = NULL;
    hailo_input_vstream input_vstreams[HEF_COUNT][MAX_EDGE_LAYERS];
    hailo_output_vstream output_vstreams[HEF_COUNT][MAX_EDGE_LAYERS];
    size_t input_frame_size[HEF_COUNT][MAX_EDGE_LAYERS];
    size_t output_frame_size[HEF_COUNT][MAX_EDGE_LAYERS];
    // Initialize 2d array to all NULL
    uint8_t *src_data[HEF_COUNT][MAX_EDGE_LAYERS];
    uint8_t *dst_data[HEF_COUNT][MAX_EDGE_LAYERS];
    size_t num_input_vstreams[HEF_COUNT];
    size_t num_output_vstreams[HEF_COUNT];
    uint8_t hef_index = 0;
    uint8_t run_index = 0;

    hailo_thread input_vstream_threads[MAX_EDGE_LAYERS];
    hailo_thread output_vstream_threads[MAX_EDGE_LAYERS];
    write_thread_args_t write_args[MAX_EDGE_LAYERS];
    read_thread_args_t read_args[MAX_EDGE_LAYERS];

    bool break_main_loop = false;

    char HEF_FILES[HEF_COUNT][MAX_HEF_PATH_LEN] = {"hefs/shortcut_net.hef", "hefs/shortcut_net.hef"};

    status = hailo_init_vdevice_params(&params);
    REQUIRE_SUCCESS(status, l_exit, "Failed init vdevice_params");

    params.device_count = DEVICE_COUNT;
    status = hailo_create_vdevice(&params, &vdevice);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create vdevice");

    for (hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        /* Select user HEFs here. In this example it's the same HEF for all networks */
        status = hailo_create_hef_file(&hef[hef_index], HEF_FILES[hef_index]);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed creating hef file %s", HEF_FILES[hef_index]);

        status = hailo_init_configure_params(hef[hef_index], HAILO_STREAM_INTERFACE_PCIE, &configure_params);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed init configure params");

        status = hailo_configure_vdevice(vdevice, hef[hef_index], &configure_params, &network_groups[hef_index], &network_groups_size);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed configuring vdevcie");
        REQUIRE_ACTION(network_groups_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef, 
            "Unexpected network group size");

        status = build_vstreams(network_groups[hef_index],
            input_vstreams[hef_index], input_frame_size[hef_index], src_data[hef_index],
            output_vstreams[hef_index], output_frame_size[hef_index], dst_data[hef_index],
            &num_input_vstreams[hef_index], &num_output_vstreams[hef_index]);
        REQUIRE_SUCCESS(status, l_release_vstreams, "Failed building streams");
    }

    // Inference part
    for (run_index = 0; run_index < RUN_COUNT; run_index++) {
        for (hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
            status = hailo_activate_network_group(network_groups[hef_index], NULL, &activated_network_group);
            REQUIRE_SUCCESS(status, l_release_vstreams, "Failed activate network group");

            for (size_t i = 0; i < num_input_vstreams[hef_index]; i++) {
                status = create_input_vstream_thread(input_vstreams[hef_index][i], src_data[hef_index][i],
                    input_frame_size[hef_index][i], &input_vstream_threads[i], &write_args[i]);
            }

            for (size_t i = 0; i < num_output_vstreams[hef_index]; i++) {
                status = create_output_vstream_thread(output_vstreams[hef_index][i], dst_data[hef_index][i],
                    output_frame_size[hef_index][i], &output_vstream_threads[i], &read_args[i]);
            }

            for (size_t i = 0; i < num_input_vstreams[hef_index]; i++) {
                status = hailo_join_thread(&input_vstream_threads[i]);
                if (HAILO_SUCCESS != status) {
                    printf("write_thread failed \n");
                    break_main_loop = true;
                }
            }

            for (size_t i = 0; i < num_output_vstreams[hef_index]; i++) {
                status = hailo_join_thread(&output_vstream_threads[i]);
                if (HAILO_SUCCESS != status) {
                    printf("write_thread failed \n");
                    break_main_loop = true;
                }
            }

            status = hailo_deactivate_network_group(activated_network_group);
            REQUIRE_SUCCESS(status, l_deactivate_network_group, "Failed to de-activate network group");

            if(break_main_loop) {
                goto l_release_vstreams;
            }
        }
    }

    printf("Inference ran successfully\n");
    status = HAILO_SUCCESS;
    goto l_release_vstreams;

l_deactivate_network_group:
    (void)hailo_deactivate_network_group(activated_network_group);
l_release_vstreams:
    for (hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        (void)hailo_release_output_vstreams(output_vstreams[hef_index], num_output_vstreams[hef_index]);
        (void)hailo_release_input_vstreams(input_vstreams[hef_index], num_input_vstreams[hef_index]);
    }

    for (hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        for (size_t i = 0; i < num_input_vstreams[hef_index]; i++) {
            if (NULL != src_data[hef_index] && NULL != src_data[hef_index][i]) {
                FREE(src_data[hef_index][i]);
            }
        }
        for (size_t i = 0; i < num_output_vstreams[hef_index]; i++) {
            if (NULL != dst_data[hef_index] && NULL != dst_data[hef_index][i]) {
                FREE(dst_data[hef_index][i]);
            }
        }
    }
l_release_hef:
    for (hef_index = 0; hef_index < HEF_COUNT; hef_index++) {
        if (NULL != hef[hef_index]) {
            (void)hailo_release_hef(hef[hef_index]);            
        }
    }
    (void)hailo_release_vdevice(vdevice);
l_exit:
    return status;
}