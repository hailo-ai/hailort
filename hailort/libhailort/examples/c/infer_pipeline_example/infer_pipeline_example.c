/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_pipeline_example.c
 * This example demonstrates the basic data-path on HailoRT using the high level API - Virtual Stream Pipeline.
 * The program scans for Hailo devices connected to a provided PCIe interface, generates a random dataset,
 * and runs it through the device with virtual streams pipeline.
 **/

#include "common.h"
#include "string.h"
#include "hailo/hailort.h"

#define INFER_FRAME_COUNT (100)
#define MAX_EDGE_LAYERS (16)
#define HEF_FILE ("hefs/shortcut_net.hef")

hailo_status infer(hailo_configured_network_group configured_network_group,
    hailo_input_vstream_params_by_name_t *input_params, hailo_output_vstream_params_by_name_t *output_params,
    hailo_vstream_info_t *vstreams_infos, size_t vstreams_infos_size)
{
    size_t frames_count = INFER_FRAME_COUNT;
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_stream_raw_buffer_by_name_t input_buffer = {0};
    hailo_stream_raw_buffer_by_name_t output_buffer = {0};
    uint8_t *src_data = NULL;
    uint8_t *dst_data = NULL;
    size_t frame_size = 0;


    for (size_t i = 0; i < vstreams_infos_size; i++) {
        if (HAILO_H2D_STREAM == vstreams_infos[i].direction) {
            memcpy(input_buffer.name, vstreams_infos[i].name, HAILO_MAX_STREAM_NAME_SIZE);
            status = hailo_get_vstream_frame_size(&(vstreams_infos[i]), &(vstreams_infos[i].format), &frame_size);
            REQUIRE_SUCCESS(status, l_free_buffers, "Failed getting input virtual stream frame size");
            input_buffer.raw_buffer.size = (frame_size * frames_count);
            src_data = malloc(input_buffer.raw_buffer.size);
            REQUIRE_ACTION(src_data != NULL, status = HAILO_OUT_OF_HOST_MEMORY, l_free_buffers, "Failed to allocate input buffer");
            // Prepare src data here
            for (size_t j = 0; j < input_buffer.raw_buffer.size; j++) {
                src_data[j] = (uint8_t)(rand() % 256);
            }

            input_buffer.raw_buffer.buffer = src_data;
        } else {
            memcpy(output_buffer.name, vstreams_infos[i].name, HAILO_MAX_STREAM_NAME_SIZE);
            status = hailo_get_vstream_frame_size(&(vstreams_infos[i]), &(vstreams_infos[i].format), &frame_size);
            REQUIRE_SUCCESS(status, l_free_buffers, "Failed getting output virtual stream frame size");
            output_buffer.raw_buffer.size = (frame_size * frames_count);
            dst_data = malloc(output_buffer.raw_buffer.size);
            REQUIRE_ACTION(dst_data != NULL, status = HAILO_OUT_OF_HOST_MEMORY, l_free_buffers, "Failed to allocate output buffer");

            output_buffer.raw_buffer.buffer = dst_data;
        }
    }

    status = hailo_infer(configured_network_group,
        input_params, &input_buffer, 1,
        output_params, &output_buffer, 1,
        frames_count);
    REQUIRE_SUCCESS(status, l_free_buffers, "Inference failure");

l_free_buffers:
    FREE(dst_data);
    FREE(src_data);

    return HAILO_SUCCESS;
}

int main()
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_vdevice vdevice = NULL;
    hailo_hef hef = NULL;
    hailo_configure_params_t config_params = {0};
    hailo_configured_network_group network_group = NULL;
    size_t network_group_size = 1;
    hailo_input_vstream_params_by_name_t input_vstream_params[MAX_EDGE_LAYERS] = {0};
    hailo_output_vstream_params_by_name_t output_vstream_params[MAX_EDGE_LAYERS] = {0};
    size_t input_vstreams_size = MAX_EDGE_LAYERS;
    size_t output_vstreams_size = MAX_EDGE_LAYERS;
    size_t vstreams_infos_size = MAX_EDGE_LAYERS;
    hailo_vstream_info_t vstreams_infos[MAX_EDGE_LAYERS] = {0};
    bool unused = {0};


    status = hailo_create_vdevice(NULL, &vdevice);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create vdevice");

    status = hailo_create_hef_file(&hef, HEF_FILE);
    REQUIRE_SUCCESS(status, l_release_vdevice, "Failed reading hef file");

    status = hailo_init_configure_params_by_vdevice(hef, vdevice, &config_params);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed initializing configure parameters");

    status = hailo_configure_vdevice(vdevice, hef, &config_params, &network_group, &network_group_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed configure vdevice from hef");
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

    status = hailo_hef_get_all_vstream_infos(hef, NULL, vstreams_infos, &vstreams_infos_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed getting virtual stream infos");
    REQUIRE_ACTION(vstreams_infos_size == 2, status = HAILO_INVALID_ARGUMENT, l_release_hef, 
        "Invalid number of virtual streams size");

    status = infer(network_group, input_vstream_params, output_vstream_params, vstreams_infos, vstreams_infos_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed running inference");

    printf("Inference ran successfully\n");
    status = HAILO_SUCCESS;

l_release_hef:
    (void) hailo_release_hef(hef);
l_release_vdevice:
    (void) hailo_release_vdevice(vdevice);
l_exit:
    return (int)status;
}
