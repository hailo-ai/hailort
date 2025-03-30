/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file data_quantization_example.c
 * This example demonstrates using quantization on an HEF network with multiple inputs and multiple outputs
 **/

#include "common.h"
#include "hailo_thread.h"
#include "hailo/hailort.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#define INFER_FRAME_COUNT (100)
#define MAX_EDGE_LAYERS (16)
#define DEVICE_COUNT (1)
#define IN_ARG "in"
#define OUT_ARG "out"
#define BOTH_ARG "both"
#define NONE_ARG "none"
#define HEF_FILE ("hefs/shortcut_net.hef")

#define USAGE_ERROR_MSG ("Args parsing error.\nUsage: data_quantization_example [in/out/both/none]\n" \
    "* in - The input is quantized, hence HailoRT won't quantize the input\n" \
    "* out - The output is to be left quantized, hence HailoRT won't de-quantize the output\n" \
    "* both - The input is quantized and the output is to be left quantized, hence HailoRT won't do either\n" \
    "* none - The input isn't quantized and the output is to be de-quantized, hence HailoRT will do both\n")

#define RAND_FLOAT32 ((float32_t)rand() / (float32_t)RAND_MAX)
#define RAND_UINT8 ((uint8_t)(rand() % 256))

typedef struct quantization_args_t {
    bool is_input_quantized;
    bool is_output_quantized;
} quantization_args_t;

typedef struct write_thread_args_t {
    hailo_input_vstream input_stream;
    quantization_args_t quant_args;
} write_thread_args_t;

void parse_arguments(int argc, char **argv, quantization_args_t *quant_args)
{
    if (2 != argc) {
        printf(USAGE_ERROR_MSG);
        exit(1);
    }

    if (0 == strncmp(IN_ARG, argv[1], ARRAY_LENGTH(IN_ARG))) {
        quant_args->is_input_quantized  = true;
        quant_args->is_output_quantized = false;
    } else if (0 == strncmp(OUT_ARG, argv[1], ARRAY_LENGTH(OUT_ARG))) {
        quant_args->is_input_quantized  = false;
        quant_args->is_output_quantized = true;
    } else if (0 == strncmp(BOTH_ARG, argv[1], ARRAY_LENGTH(BOTH_ARG))) {
        quant_args->is_input_quantized  = true;
        quant_args->is_output_quantized = true;
    } else if (0 == strncmp(NONE_ARG, argv[1], ARRAY_LENGTH(NONE_ARG))) {
        quant_args->is_input_quantized  = false;
        quant_args->is_output_quantized = false;
    } else {
        printf(USAGE_ERROR_MSG);
        exit(1);
    }
}

uint8_t* get_random_input_data(size_t host_input_frame_size, bool is_input_quantized)
{
    uint8_t *input_buffer = (uint8_t*)malloc(host_input_frame_size);
    if (NULL == input_buffer) {
        printf("Out of host memory\b");
        return NULL;
    }

    if (is_input_quantized) {
        // Input data is expected to be quantized so we generate a random uint8_t buffer
        for (size_t i = 0; i < host_input_frame_size; i++) {
            input_buffer[i] = RAND_UINT8;
        }
    } else {
        // Input data isn't to be quantized in advance, rather it'll be quantized by the HailoRT runtime.
        // Note that the 'format_type' param passed to 'hailo_make_input_vstream_params()' in 'create_vstreams()'
        // is HAILO_FORMAT_TYPE_FLOAT32, so we create a buffer of random float32_t that'll be cast to uint8_t*
        // as expected by the HailoRT API. Internally, the float32_t will be quantized and sent to the device
        // as uint8_t. Also note that host_input_frame_size is already calculated considering that the 'format_type'
        // is HAILO_FORMAT_TYPE_FLOAT32.
        for (size_t i = 0; i < (host_input_frame_size / sizeof(float32_t)); i++) {
            // RAND_FLOAT32 will give us random values in the interval [0.0,1.0); we arbitrarily chose to expand
            // the values to the interval [0.0, 1000.0).
            ((float32_t*)input_buffer)[i] = RAND_FLOAT32 * 1000;
        }
    }

    return input_buffer;
}


thread_return_type write_to_device(void *args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t input_frame_size = 0;
    void *src_data = NULL;
    write_thread_args_t *write_args = (write_thread_args_t*)args;

    status = hailo_get_input_vstream_frame_size(write_args->input_stream, &input_frame_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed getting input virtual stream frame size");

    src_data = get_random_input_data(input_frame_size, write_args->quant_args.is_input_quantized);
    REQUIRE_ACTION(src_data != NULL, status = HAILO_OUT_OF_HOST_MEMORY, l_exit, "Failed to allocate input buffer");

    for (uint32_t i = 0; i < INFER_FRAME_COUNT; i++) {
        status = hailo_vstream_write_raw_buffer(write_args->input_stream, src_data, input_frame_size);
        REQUIRE_SUCCESS(status, l_free_buffer, "Failed sending to vstream");
    }

    status = HAILO_SUCCESS;
l_free_buffer:
    FREE(src_data);
l_exit:
    return (thread_return_type)status;
}

thread_return_type read_from_device(void *vstream_ptr)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t output_frame_size = 0;
    uint8_t *dst_data = NULL;
    hailo_output_vstream vstream = *(hailo_output_vstream*)vstream_ptr;

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
    hailo_output_vstream *output_vstreams, size_t output_vstreams_size, quantization_args_t quant_args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_thread write_threads [MAX_EDGE_LAYERS] = {0};
    hailo_thread read_threads [MAX_EDGE_LAYERS] = {0};
    hailo_status write_thread_status = HAILO_UNINITIALIZED;
    hailo_status read_thread_status = HAILO_UNINITIALIZED;
    write_thread_args_t write_args[MAX_EDGE_LAYERS] = {0};
    size_t input_stream_index = 0;
    size_t output_stream_index = 0;
    size_t i = 0;

    // Create reading threads
    for (output_stream_index = 0; output_stream_index < output_vstreams_size; output_stream_index++) {
        status = hailo_create_thread(read_from_device, &output_vstreams[output_stream_index],
            &read_threads[output_stream_index]);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");
    }

    // Create writing threads
    for (input_stream_index = 0; input_stream_index < input_vstreams_size; input_stream_index++) {
        write_args[input_stream_index].input_stream  = input_vstreams[input_stream_index];
        write_args[input_stream_index].quant_args    = quant_args;

        status = hailo_create_thread(write_to_device, &write_args[input_stream_index],
            &write_threads[input_stream_index]);
        REQUIRE_SUCCESS(status, l_cleanup, "Failed creating thread");
    }

l_cleanup:
    // Join write threads 
    for (i = 0; i < input_stream_index; i++) {
        write_thread_status = hailo_join_thread(&write_threads[i]);
        if (HAILO_SUCCESS != write_thread_status) {
            printf("write_thread failed \n");
            status = write_thread_status; // Override write status
        }
    }

    // Join read threads 
    for (i = 0; i < output_stream_index; i++) {
        read_thread_status = hailo_join_thread(&read_threads[i]);
        if (HAILO_SUCCESS != read_thread_status) {
            printf("read_thread failed \n");
            status = read_thread_status; // Override read status
        }
    }

    return status;
}

hailo_status create_vstreams(hailo_configured_network_group network_group, hailo_input_vstream *input_vstreams,
    size_t *input_vstreams_size, hailo_output_vstream *output_vstreams, size_t *output_vstreams_size,
    quantization_args_t quant_args)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_input_vstream_params_by_name_t input_vstreams_params[MAX_EDGE_LAYERS] = {0};
    hailo_output_vstream_params_by_name_t output_vstreams_params[MAX_EDGE_LAYERS] = {0};
    const hailo_format_type_t input_type = (quant_args.is_input_quantized ? HAILO_FORMAT_TYPE_UINT8 :
        HAILO_FORMAT_TYPE_FLOAT32);
    const hailo_format_type_t output_type = HAILO_FORMAT_TYPE_UINT8;

    status = hailo_make_input_vstream_params(network_group, quant_args.is_input_quantized, input_type,
        input_vstreams_params, input_vstreams_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed making input virtual stream params");

    status = hailo_make_output_vstream_params(network_group, quant_args.is_output_quantized, output_type,
        output_vstreams_params, output_vstreams_size);
    REQUIRE_SUCCESS(status, l_exit, "Failed making output virtual stream params");

    REQUIRE_ACTION((*input_vstreams_size <= MAX_EDGE_LAYERS || *output_vstreams_size <= MAX_EDGE_LAYERS),
        status = HAILO_INVALID_OPERATION, l_exit, "Trying to infer network with too many input/output virtual streams, "
        "Maximum amount is %d, (either change HEF or change the definition of MAX_EDGE_LAYERS)\n", MAX_EDGE_LAYERS);

    status = hailo_create_input_vstreams(network_group, input_vstreams_params,
        *input_vstreams_size, input_vstreams);
    REQUIRE_SUCCESS(status, l_exit, "Failed creating input virtual streams");

    status = hailo_create_output_vstreams(network_group, output_vstreams_params,
        *output_vstreams_size, output_vstreams);
    REQUIRE_SUCCESS(status, l_release_input_vstream, "Failed creating output virtual streams");

    status = HAILO_SUCCESS;
    goto l_exit;
l_release_input_vstream:
    (void) hailo_release_input_vstreams(input_vstreams, *input_vstreams_size);
l_exit:
    return status;
}

int main(int argc, char **argv)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_vdevice vdevice = NULL;
    hailo_vdevice_params_t params = {0};
    hailo_hef hef = NULL;
    hailo_configure_params_t config_params = {0};
    hailo_configured_network_group network_group = NULL;
    size_t network_group_size = 1;
    hailo_input_vstream input_vstreams[MAX_EDGE_LAYERS] = {NULL};
    hailo_output_vstream output_vstreams[MAX_EDGE_LAYERS] = {NULL};
    size_t input_vstreams_size = MAX_EDGE_LAYERS;
    size_t output_vstreams_size = MAX_EDGE_LAYERS;
    quantization_args_t quant_args;

    parse_arguments(argc, argv, &quant_args);

    status = hailo_init_vdevice_params(&params);
    REQUIRE_SUCCESS(status, l_exit, "Failed init vdevice_params");

    params.device_count = DEVICE_COUNT;
    status = hailo_create_vdevice(&params, &vdevice);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create vdevice");

    status = hailo_create_hef_file(&hef, HEF_FILE);
    REQUIRE_SUCCESS(status, l_release_vdevice, "Failed reading hef file");

    status = hailo_init_configure_params_by_vdevice(hef, vdevice, &config_params);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed initializing configure parameters");

    status = hailo_configure_vdevice(vdevice, hef, &config_params, &network_group, &network_group_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed configure vdevcie from hef");
    REQUIRE_ACTION(network_group_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef,
        "Invalid network group size");

    status = create_vstreams(network_group, input_vstreams, &input_vstreams_size, output_vstreams,
        &output_vstreams_size,quant_args);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed creating virtual streams");

    status = infer(input_vstreams, input_vstreams_size, output_vstreams, output_vstreams_size, quant_args);
    REQUIRE_SUCCESS(status, l_release_vstreams, "Inference failure");

    printf("Inference ran successfully\n");
    status = HAILO_SUCCESS;

l_release_vstreams:
    (void)hailo_release_output_vstreams(output_vstreams, output_vstreams_size);
    (void)hailo_release_input_vstreams(input_vstreams, input_vstreams_size);
l_release_hef:
    (void) hailo_release_hef(hef);
l_release_vdevice:
    (void) hailo_release_vdevice(vdevice);
l_exit:
    return (int)status;
}
