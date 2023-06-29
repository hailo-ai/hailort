/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file nms_stream_reader.cpp
 * @brief static class that helps receive and read the nms ouput stream according to the different burst mode, type and size.
 * 
 * Explanation of state machine and logic:
 * This class supports the following 5 nms cases:
 *  1) Hailo-8 bbox mode (non burst mode)
 *  2) Hailo-15 bbox mode
 *  3) Hailo-8 Burst mode 
 *  4) Hailo-15 Burst per class mode
 *  5) Hailo15 Burst per frame mode
 * 
 * Lets explain each mode and the state machine of each mode:
 * 1)-2) Hailo-8 bbox mode / Hailo-15 bbox mode - both work the same - they read bbox bbox from the nms core until a delimeter comes
 *       and expect to read the amount of delimeters as the same amount of number of classes (times num chunks if more than one chunk per frame).
 * 
 * 3) Hailo8 Burst mode - Hailo 8 burst mode reads bursts in the size of burst-size and expects each burst to be made of x bboxes and
 *    then a delimeter and padding until the end of the burst - essentially what the state machine does here is read until the first delimeter
 *    and then expect padding until end of burts (in release mode we dont check that the rest of burst is padding and
 *    just go onto the next burst but in debug we validate that rest of burst is padding). NOTE: in Hailo-8 delimeter value and
 *    padding value are both 0xFFFFFFFFFFFFFFFF so essentially we read until first delimeter - and the every following delimeter
 *    in burst is padding. This mode also supports interrupt per frame - assuming burst size received from SDK is larger than max bboxes + 1 (for delimeter)
 *    we know there will be one burst per class and hence the output size will be num classes * burst size and we enable one interrupt per frame.
 * 
 * 4) Hailo15 Burst per class mode - Hailo-15 Burst per class mode reads bursts in the size of burst size and expects the following order.
 *    x bboxes , followed by a delimeter, followed by an image delimeter, followed by padding until the end of the burst. The bbboxes, delimeter
 *    and image delimeter can all be in different bursts - so essentially the way the state machine works is the following: we read burst burst,
 *    in each burst we iterate over the bboxes until we find a delimeter - once after that we know how many bboxes there were for that class,
 *    and then we expect to see a following image delimeter after the delimeter, once we read the image delimeter we expect padding until the end of the
 *    burst (which we ensure in debug but not in release). NOTE: if a burst ends on a delimeter we need to read the next burst to get the image delimeter
 *    even in the case where the amount of delimeters we read is equal to the amount of classes - otherwise there is data still in the core
 *    that was not emptied and will be read as part of the next frame. This mode also supports interrupt per frame - assuming burst size received from SDK
 *    is larger than max bboxes + 2 (for image delimeter and delimeter) we know there will be one burst per class and hence the output size will be 
 *    num classes * burst size and we enable one interrupt per frame.
 * 
 * 5) Hailo15 Burst per frame mode - Hailo-15 Burst per frame mode reads bursts in the size of burst size and expects the following order.
 *    x bboxes , followed by a delimeter, for all the classes until the last class where the last delimeter should be followed by an image delimeter
 *    and padding until the end of the burst. The state machine works in the following way - we read burst burst, and for each time we reach a delimeter
 *    we save the amount of bboxes that were read for that class and keep reading the burst. NOTE: this is the only mode where there can be multiple
 *    delimeters per burst. Once we read the last delimeter (which we know from number classes) - we ensure there is a following image delimeter (which again
 *    can be in the following burst) and then assume the rest of the burst is padding (and in debug we verify that). NOTE: currently this mode is not
 *    supported in the sdk.
 * 
 **/

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "stream_common/nms_stream_reader.hpp"
#include "src/hef/layer_info.hpp"

namespace hailort
{

static void finish_reading_burst_update_state(NMSBurstState *burst_state, bool *can_stop_reading_burst, size_t *burst_index)
{
    *burst_state = NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER;
    *burst_index = (*burst_index + 1);
    *can_stop_reading_burst = true;
}

// Function that implements the state machine of the 3 different nms burst modes based on the value of the current bbox and the current state.
hailo_status NMSStreamReader::advance_state_machine(NMSBurstState *burst_state, const uint64_t current_bbox,
    const hailo_nms_burst_type_t burst_type, const uint32_t num_classes, size_t *num_delimeters_received,
    bool *can_stop_reading_burst, const size_t burst_offset, const size_t burst_size, size_t *burst_index)
{
    switch(current_bbox) {
        // This is also case for Hailo8 padding - seeing as they are same value
        case NMS_DELIMITER:
        {
            // If we are in hailo8 per class mode - if we are in state waiting for delimeter - we received delimeter
            // otherwise we must be in state waiting for padding - in which case we received padding.
            if (HAILO_BURST_TYPE_H8_PER_CLASS == burst_type) {
                CHECK_IN_DEBUG((NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER == (*burst_state)) ||
                    (NMSBurstState::NMS_BURST_STATE_WAITING_FOR_PADDING == (*burst_state)), HAILO_NMS_BURST_INVALID_DATA,
                    "Invalid state, H8 NMS burst cannot receive delimeter while in state {}", (*burst_state));
                // To differentiate from H8 padding - where we should not increment amount of delimeters found
                if ((*burst_state) == NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER) {
                    (*num_delimeters_received)++;
                }
#ifdef NDEBUG
                // In hailo8 burst mode - if is in state waiting for delimeter and got delimeter - rest will be padding and can skip
                if ((*burst_state) == NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER) {
                    finish_reading_burst_update_state(burst_state, can_stop_reading_burst, burst_index);
                    break;
                }
#endif
                // In hailo8 mode after delimeter we expect padding until end of burst - seeing as h8 padding is same value
                // Weather was in state wait for delimeter or state wait for padding - will always go to wait for padding until end of burst
                *burst_state = NMSBurstState::NMS_BURST_STATE_WAITING_FOR_PADDING;
                if (burst_offset == (burst_size - sizeof(current_bbox))) {
                    finish_reading_burst_update_state(burst_state, can_stop_reading_burst, burst_index);
                }
                break;

            } else if (HAILO_BURST_TYPE_H15_PER_CLASS == burst_type) {
                CHECK_IN_DEBUG(NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER == (*burst_state), HAILO_NMS_BURST_INVALID_DATA,
                    "Invalid state, H15 Per class NMS burst cannot receive delimeter while in state {}", (*burst_state));
                (*num_delimeters_received)++;
                *burst_state = NMSBurstState::NMS_BURST_STATE_WAITING_FOR_IMAGE_DELIMETER;
            } else {
                CHECK_IN_DEBUG(NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER == (*burst_state), HAILO_NMS_BURST_INVALID_DATA,
                    "Invalid state, H15 Per Frame NMS burst cannot receive delimeter while in state {}", (*burst_state));
                // in hailo15 per frame - if number of delimeter is same as num classes - we expect image delimeter next 
                // otherwise expect another delimeter
                (*num_delimeters_received)++;
                if (num_classes == (*num_delimeters_received)) {
                    *burst_state = NMSBurstState::NMS_BURST_STATE_WAITING_FOR_IMAGE_DELIMETER;
                }
            }
            break;
        }

        case NMS_IMAGE_DELIMITER:
        {
            CHECK_IN_DEBUG(HAILO_BURST_TYPE_H8_PER_CLASS != burst_type, HAILO_NMS_BURST_INVALID_DATA,
                "Invalid state, H8 NMS burst cannot receive image delimeter");

            CHECK_IN_DEBUG(NMSBurstState::NMS_BURST_STATE_WAITING_FOR_IMAGE_DELIMETER == (*burst_state), HAILO_NMS_BURST_INVALID_DATA,
                "Invalid state, H15 NMS burst cannot receive image delimeter in state {}", (*burst_state));
            
            // in both hailo15 per class and per frame - when receiving image delimeter we move to expecting padding
            *burst_state = NMSBurstState::NMS_BURST_STATE_WAITING_FOR_PADDING;
    
#ifdef NDEBUG
            finish_reading_burst_update_state(burst_state, can_stop_reading_burst, burst_index);
#endif // NDEBUG
            break;
        }

        case NMS_H15_PADDING:
        {
            if ((HAILO_BURST_TYPE_H15_PER_CLASS == burst_type) || (HAILO_BURST_TYPE_H15_PER_FRAME == burst_type)) {
                CHECK_IN_DEBUG(NMSBurstState::NMS_BURST_STATE_WAITING_FOR_PADDING == (*burst_state), HAILO_NMS_BURST_INVALID_DATA,
                    "Invalid state, H15 NMS burst cannot receive padding in state {}", (*burst_state));
            }
            // In case of padding next state is wait for padding unless it is last padding of burst - then next state will be
            // Wait for delimeter - will only get to this stage in debug - in release once image delimeter is read we ignore rest of
            // burst seeing as it must be padding
            if (burst_offset == (burst_size - sizeof(current_bbox))) {
                finish_reading_burst_update_state(burst_state, can_stop_reading_burst, burst_index);
            }
            break;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status NMSStreamReader::read_nms_bbox_mode(OutputStream &stream, void *buffer, size_t offset)
{
    const uint32_t num_classes = stream.get_info().nms_info.number_of_classes;
    const uint32_t chunks_per_frame = stream.get_info().nms_info.chunks_per_frame;
    const size_t bbox_size = stream.get_info().nms_info.bbox_size;
    
    for (size_t delimeters_found = 0; delimeters_found < (num_classes * chunks_per_frame); delimeters_found++) {
        nms_bbox_counter_t class_bboxes_count = 0;
        nms_bbox_counter_t* class_bboxes_count_ptr = (nms_bbox_counter_t*)(reinterpret_cast<uint8_t*>(buffer) + offset);
        offset += sizeof(*class_bboxes_count_ptr);

        while (true) {
            MemoryView buffer_view(static_cast<uint8_t*>(buffer) + offset, bbox_size);
            auto status = stream.read_impl(buffer_view);
            if ((HAILO_STREAM_ABORTED_BY_USER == status) ||
                ((HAILO_STREAM_NOT_ACTIVATED == status))) {
                return status;
            }
            CHECK_SUCCESS(status, "Failed reading nms bbox");
            const uint64_t current_bbox = *(uint64_t*)((uint8_t*)buffer + offset);

            if (NMS_IMAGE_DELIMITER == current_bbox) {
                continue;
            }

            if (NMS_DELIMITER == current_bbox) {
                break;
            }

            class_bboxes_count++;
            CHECK_IN_DEBUG(class_bboxes_count <= stream.get_info().nms_info.max_bboxes_per_class, HAILO_INTERNAL_FAILURE,
                "Data read from the device for the current class was size {}, max size is {}", class_bboxes_count,
                stream.get_info().nms_info.max_bboxes_per_class);
            offset += bbox_size;
        }

        *class_bboxes_count_ptr = class_bboxes_count;
    }

    return HAILO_SUCCESS;
}

hailo_status NMSStreamReader::read_nms_burst_mode(OutputStream &stream, void *buffer, size_t offset, size_t buffer_size)
{
    NMSBurstState burst_state = NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER;
    const uint32_t bbox_size = stream.get_info().nms_info.bbox_size;
    const size_t burst_size = stream.get_layer_info().nms_info.burst_size * bbox_size;
    const hailo_nms_burst_type_t burst_type = stream.get_layer_info().nms_info.burst_type;
    const auto num_expected_delimeters = stream.get_info().nms_info.chunks_per_frame * stream.get_info().nms_info.number_of_classes;
    // Transfer size if affected from if working in interrupt per burst or interrupt per frame
    const size_t transfer_size = LayerInfoUtils::get_nms_layer_transfer_size(stream.get_layer_info());
    const bool is_interrupt_per_frame = (transfer_size > burst_size);

    CHECK(bbox_size == sizeof(uint64_t), HAILO_INTERNAL_FAILURE,
        "Invalid Bbox size, must be 8 bytes received {}", bbox_size);

    CHECK(transfer_size <= buffer_size, HAILO_INTERNAL_FAILURE, "Invalid transfer size {}, Cannot be larger than buffer {}",
        transfer_size, buffer_size);

    // Start writing bboxes at offset sizeof(nms_bbox_counter_t) - because the first sizeof(nms_bbox_counter_t) will be
    // used to write amount of bboxes found for class 0 etc...
    nms_bbox_counter_t class_bboxes_count = 0;
    nms_bbox_counter_t* class_bboxes_count_ptr = (nms_bbox_counter_t*)(reinterpret_cast<uint8_t*>(buffer) + offset);
    offset += sizeof(nms_bbox_counter_t);

    // Counter of number of delimeters found in frame
    size_t delimeters_found = 0;
    size_t burst_index = 0;
    uint8_t *start_index_of_burst_in_buffer = nullptr;
    while ((delimeters_found < num_expected_delimeters) || (NMSBurstState::NMS_BURST_STATE_WAITING_FOR_IMAGE_DELIMETER == burst_state)) {
        // In interrupt per frame we read whole frame once (in first iteration) - then don't read in following loop iterations
        // delimeters_found will always be 0 in first iteration - and in interrupt_per_frame will always be larger in following iterations
        if (!is_interrupt_per_frame || (0 == delimeters_found)) {
            assert(offset + transfer_size <= buffer_size);
            start_index_of_burst_in_buffer = static_cast<uint8_t*>(buffer) + offset;
            MemoryView buffer_view(start_index_of_burst_in_buffer, transfer_size);
            auto status = stream.read_impl(buffer_view);
            if ((HAILO_STREAM_ABORTED_BY_USER == status) || ((HAILO_STREAM_NOT_ACTIVATED == status))) {
                return status;
            }
            CHECK_SUCCESS(status, "Failed reading nms burst");
        }

        // Flag that marks if we can stop reading burst and continue to next burst
        bool can_stop_reading_burst = false;
        // Iterate through burst and copy relevant data to user buffer
        for (size_t burst_offset = 0; burst_offset < burst_size; burst_offset += bbox_size) {
            uint64_t current_bbox = 0;
            if (is_interrupt_per_frame) {
                assert((burst_index * burst_size) + burst_offset < transfer_size);
                current_bbox = *(uint64_t*)((uint8_t*)start_index_of_burst_in_buffer + (burst_index * burst_size) + burst_offset);
            } else {
                current_bbox = *(uint64_t*)((uint8_t*)start_index_of_burst_in_buffer + burst_offset);
            }

            // If read delimeter - fill in information about num of bboxes found for the class (we also make sure that
            //  It is in state NMS_BURST_STATE_WAITING_FOR_DELIMETER because in hailo8 padding is same value)
            if ((NMS_DELIMITER == current_bbox) && (NMSBurstState::NMS_BURST_STATE_WAITING_FOR_DELIMETER == burst_state)) {
                *class_bboxes_count_ptr = class_bboxes_count;
                class_bboxes_count_ptr = (nms_bbox_counter_t*)(reinterpret_cast<uint8_t*>(buffer) + offset);
                class_bboxes_count = 0;
                offset += sizeof(nms_bbox_counter_t);
            }

            // Received delimeter can stop reading burst because rest of burst is image delimeter then padding
            if ((NMS_DELIMITER == current_bbox) || (NMS_IMAGE_DELIMITER == current_bbox) || (NMS_H15_PADDING == current_bbox)) {
                auto status = advance_state_machine(&burst_state, current_bbox, burst_type, stream.get_info().nms_info.number_of_classes,
                    &delimeters_found, &can_stop_reading_burst, burst_offset, burst_size, &burst_index);
                CHECK_SUCCESS(status);

                if (can_stop_reading_burst) {
                    break;
                }
                continue;
            }

            class_bboxes_count++;
            CHECK_IN_DEBUG(class_bboxes_count <= stream.get_info().nms_info.max_bboxes_per_class, HAILO_INTERNAL_FAILURE,
                "Data read from the device for the current class was size {}, max size is {}", class_bboxes_count,
                stream.get_info().nms_info.max_bboxes_per_class);
            
            // Copy bbox to correct location in buffer
            memcpy((static_cast<uint8_t*>(buffer) + offset), &current_bbox, sizeof(current_bbox));
            offset += bbox_size;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status NMSStreamReader::read_nms(OutputStream &stream, void *buffer, size_t offset, size_t size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    const bool burst_mode = (HAILO_BURST_TYPE_NO_BURST != stream.get_layer_info().nms_info.burst_type);
    if (burst_mode) {
        status = NMSStreamReader::read_nms_burst_mode(stream, buffer, offset, size);
    } else {
        status = NMSStreamReader::read_nms_bbox_mode(stream, buffer, offset);
    }
    if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return status;
    }
    CHECK_SUCCESS(status, "Failed reading nms");

    return HAILO_SUCCESS;
}

} /* namespace hailort */