/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file raw_async_streams_single_thread_example.c
 * This example demonstrates basic usage of HailoRT async streaming api with a single thread.
 **/

#include "common.h"
#include "hailo/hailort.h"

#include <string.h>

#if defined(__unix__)
#include <sys/mman.h>
#endif


#define HEF_FILE ("hefs/shortcut_net.hef")
#define MAX_EDGE_LAYERS_PER_DIR (16)
#define MAX_EDGE_LAYERS (MAX_EDGE_LAYERS_PER_DIR * 2)
#define MAX_ONGOING_TRANSFERS (16)
#define INFER_TIME_SECONDS (5)

#if defined(__unix__)
#define INVALID_ADDR (MAP_FAILED)
#define page_aligned_alloc(size) mmap(NULL, (size), PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)
#define page_aligned_free(addr, size) munmap((addr), (size))
#elif defined(_MSC_VER)
#define INVALID_ADDR (NULL)
#define page_aligned_alloc(size) VirtualAlloc(NULL, (size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
#define page_aligned_free(addr, size) VirtualFree((addr), 0, MEM_RELEASE)
#else /* defined(_MSC_VER) */
#pragma error("Aligned alloc not supported")
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif


static void output_done_callback(const hailo_stream_read_async_completion_info_t *completion_info)
{
    hailo_output_stream stream = (hailo_output_stream)completion_info->opaque;
    hailo_status status = HAILO_UNINITIALIZED;

    switch (completion_info->status) {
    case HAILO_SUCCESS:
        // Real applications can forward the buffer to post-process/display. Here we just re-launch new async reads.
        status = hailo_stream_read_raw_buffer_async(stream, completion_info->buffer_addr, completion_info->buffer_size,
            output_done_callback, stream);
        if ((HAILO_SUCCESS != status) && (HAILO_STREAM_ABORT != status)) {
            fprintf(stderr, "Failed read async with status=%d\n", status);
        }
        break;
    case HAILO_STREAM_ABORT:
        // Transfer was canceled, finish gracefully.
        break;
    default:
        fprintf(stderr, "Got an unexpected status on callback. status=%d\n", completion_info->status);
    }
}

static void input_done_callback(const hailo_stream_write_async_completion_info_t *completion_info)
{
    hailo_input_stream stream = (hailo_input_stream)completion_info->opaque;
    hailo_status status = HAILO_UNINITIALIZED;

    switch (completion_info->status) {
    case HAILO_SUCCESS:
        // Real applications may free the buffer and replace it with new buffer ready to be sent. Here we just re-launch
        // new async writes.
        status = hailo_stream_write_raw_buffer_async(stream, completion_info->buffer_addr, completion_info->buffer_size,
            input_done_callback, stream);
        if ((HAILO_SUCCESS != status) && (HAILO_STREAM_ABORT != status)) {
            fprintf(stderr, "Failed write async with status=%d\n", status);
        }
        break;
    case HAILO_STREAM_ABORT:
        // Transfer was canceled, finish gracefully.
        break;
    default:
        fprintf(stderr, "Got an unexpected status on callback. status=%d\n", completion_info->status);
    }
}

typedef struct {
    void *addr;
    size_t size;
    hailo_dma_buffer_direction_t direction;
} allocated_buffer_t;

static hailo_status infer(hailo_device device, hailo_configured_network_group network_group, size_t number_input_streams,
    hailo_input_stream *input_streams, size_t number_output_streams, hailo_output_stream *output_streams,
    size_t ongoing_transfers)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t i = 0;
    size_t frame_index = 0;
    size_t frame_size = 0;
    size_t stream_index = 0;
    void *current_buffer = NULL;

    allocated_buffer_t buffers[MAX_EDGE_LAYERS * MAX_ONGOING_TRANSFERS] = {0};
    size_t allocated_buffers = 0;

    // We launch "ongoing_transfers" async operations for both input and output streams. On each async callback, we launch
    // some new operation with the same buffer.
    for (stream_index = 0; stream_index < number_output_streams; stream_index++) {
        frame_size = hailo_get_output_stream_frame_size(output_streams[stream_index]);

        // ongoing_transfers is less than or equal to the stream's max async queue size, so we can start parallel reads.
        for (frame_index = 0; frame_index < ongoing_transfers; frame_index++) {
            // Buffers read from async operation must be page aligned.
            current_buffer = page_aligned_alloc(frame_size);
            REQUIRE_ACTION(INVALID_ADDR != current_buffer, status=HAILO_OUT_OF_HOST_MEMORY, l_shutdown, "allocation failed");
            buffers[allocated_buffers++] = (allocated_buffer_t){ current_buffer, frame_size, HAILO_DMA_BUFFER_DIRECTION_D2H };

            // If the same buffer is used multiple times on async-io, to improve performance, it is recommended to
            // pre-map it into the device.
            status = hailo_device_dma_map_buffer(device, current_buffer, frame_size, HAILO_DMA_BUFFER_DIRECTION_D2H);
            REQUIRE_SUCCESS(status, l_shutdown, "Failed map buffer with status=%d", status);

            status = hailo_stream_read_raw_buffer_async(output_streams[stream_index], current_buffer, frame_size,
                output_done_callback, output_streams[stream_index]);
            REQUIRE_SUCCESS(status, l_shutdown, "Failed read async with status=%d", status);
        }
    }

    for (stream_index = 0; stream_index < number_input_streams; stream_index++) {
        frame_size = hailo_get_input_stream_frame_size(input_streams[stream_index]);

        // ongoing_transfers is less than or equal to the stream's max async queue size, so we can start parallel writes.
        for (frame_index = 0; frame_index < ongoing_transfers; frame_index++) {
            // Buffers written to async operation must be page aligned.
            current_buffer = page_aligned_alloc(frame_size);
            REQUIRE_ACTION(INVALID_ADDR != current_buffer, status=HAILO_OUT_OF_HOST_MEMORY, l_shutdown, "allocation failed");
            buffers[allocated_buffers++] = (allocated_buffer_t){ current_buffer, frame_size, HAILO_DMA_BUFFER_DIRECTION_H2D };

            // If the same buffer is used multiple times on async-io, to improve performance, it is recommended to
            // pre-map it into the device.
            status = hailo_device_dma_map_buffer(device, current_buffer, frame_size, HAILO_DMA_BUFFER_DIRECTION_H2D);
            REQUIRE_SUCCESS(status, l_shutdown, "Failed map buffer with status=%d", status);

            status = hailo_stream_write_raw_buffer_async(input_streams[stream_index], current_buffer, frame_size,
                input_done_callback, input_streams[stream_index]);
            REQUIRE_SUCCESS(status, l_shutdown, "Failed write async with status=%d", status);
        }
    }

    // After all async operations are launched, the inference will continue until we shutdown the network.
    hailo_sleep(INFER_TIME_SECONDS);

    status = HAILO_SUCCESS;
l_shutdown:
    // Calling hailo_shutdown_network_group will ensure that all async operations are done. All pending async I/O
    // operations will be canceled and their callbacks called with status=HAILO_STREAM_ABORT.
    (void) hailo_shutdown_network_group(network_group);

    // There are no async I/O operations ongoing so it is safe to free the buffers now.
    for (i = 0; i < allocated_buffers; i++) {
        (void) hailo_device_dma_unmap_buffer(device, buffers[i].addr, buffers[i].size, buffers[i].direction);
        page_aligned_free(buffers[i].addr, buffers[i].size);
    }

    return status;
}

static hailo_status configure_device(hailo_device device, const char *hef_file,
    hailo_configured_network_group *network_group)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_hef hef = NULL;
    hailo_configure_params_t configure_params = {0};
    size_t i = 0;
    size_t network_group_size = 1;

    // Load HEF file.
    status = hailo_create_hef_file(&hef, hef_file);
    REQUIRE_SUCCESS(status, l_exit, "Failed reading hef file %s", hef_file);

    // Create configure params
    status = hailo_init_configure_params_by_device(hef, device, &configure_params);
    REQUIRE_SUCCESS(status, l_exit, "Failed init configure params");
    REQUIRE_ACTION(configure_params.network_group_params_count == 1, status=HAILO_INVALID_ARGUMENT, l_exit,
        "Unexpected network group size");

    // Set HAILO_STREAM_FLAGS_ASYNC for all streams in order to use async api.
    for (i = 0; i < configure_params.network_group_params[0].stream_params_by_name_count; i++) {
        configure_params.network_group_params[0].stream_params_by_name[i].stream_params.flags = HAILO_STREAM_FLAGS_ASYNC;
    }

    status = hailo_configure_device(device, hef, &configure_params, network_group, &network_group_size);
    REQUIRE_SUCCESS(status, l_release_hef, "Failed configuring device");

    status = HAILO_SUCCESS;
l_release_hef:
    (void) hailo_release_hef(hef);
l_exit:
    return status;
}

int main()
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_device device = NULL;
    hailo_configured_network_group network_group = NULL;
    hailo_stream_info_t input_streams_info[MAX_EDGE_LAYERS_PER_DIR] = {0};
    hailo_stream_info_t output_streams_info[MAX_EDGE_LAYERS_PER_DIR] = {0};
    hailo_input_stream input_streams[MAX_EDGE_LAYERS_PER_DIR] = {NULL};
    hailo_output_stream output_streams[MAX_EDGE_LAYERS_PER_DIR] = {NULL};
    size_t number_input_streams = 0;
    size_t number_output_streams = 0;
    size_t index = 0;
    size_t queue_size = 0;
    size_t ongoing_transfers = MAX_ONGOING_TRANSFERS;
    hailo_activated_network_group activated_network_group = NULL;

    // Create device object.
    status = hailo_create_device_by_id(NULL, &device);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create device");

    // Configure device with HEF.
    status = configure_device(device, HEF_FILE, &network_group);
    REQUIRE_SUCCESS(status, l_release_device, "Failed configure_device");

    // Get input/output stream objects.
    status = hailo_network_group_get_input_stream_infos(network_group, input_streams_info, MAX_EDGE_LAYERS_PER_DIR,
        &number_input_streams);
    REQUIRE_SUCCESS(status, l_release_device, "Failed getting input streams infos");

    status = hailo_network_group_get_output_stream_infos(network_group, output_streams_info, MAX_EDGE_LAYERS_PER_DIR,
        &number_output_streams);
    REQUIRE_SUCCESS(status, l_release_device, "Failed getting output streams infos");

    for (index = 0; index < number_input_streams; index++) {
        status = hailo_get_input_stream(network_group, input_streams_info[index].name, &input_streams[index]);
        REQUIRE_SUCCESS(status, l_release_device, "Failed getting input stream %s", input_streams_info[index].name);

        status = hailo_input_stream_get_async_max_queue_size(input_streams[index], &queue_size);
        REQUIRE_SUCCESS(status, l_release_device, "Failed getting queue size");

        ongoing_transfers = MIN(queue_size, ongoing_transfers);
    }

    for (index = 0; index < number_output_streams; index++) {
        status = hailo_get_output_stream(network_group, output_streams_info[index].name, &output_streams[index]);
        REQUIRE_SUCCESS(status, l_release_device, "Failed getting output stream %s", output_streams_info[index].name);

        status = hailo_output_stream_get_async_max_queue_size(output_streams[index], &queue_size);
        REQUIRE_SUCCESS(status, l_release_device, "Failed getting queue size");

        ongoing_transfers = MIN(queue_size, ongoing_transfers);
    }

    // Activate network group
    status = hailo_activate_network_group(network_group, NULL, &activated_network_group);
    REQUIRE_SUCCESS(status, l_release_device, "Failed activate network group");

    // Run infer.
    status = infer(device, network_group, number_input_streams, input_streams, number_output_streams, output_streams,
        ongoing_transfers);
    REQUIRE_SUCCESS(status, l_deactivate, "Failed performing inference");

    status = HAILO_SUCCESS;
    printf("Inference ran successfully\n");

l_deactivate:
    (void) hailo_deactivate_network_group(activated_network_group);
l_release_device:
    (void) hailo_release_device(device);
l_exit:
    return (int)status;
}
