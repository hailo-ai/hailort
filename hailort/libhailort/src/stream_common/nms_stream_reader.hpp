/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file nms_stream_reader.hpp
 * @brief static class that helps receives and reads the nms ouput stream according to the differnet burst mode, type and size.
 * 
 * For explanation on the different burst modes and types and state machine and logic of the class please check out the cpp.
 * 
 **/

#ifndef _NMS_STREAM_READER_HPP_
#define _NMS_STREAM_READER_HPP_

#include "hailo/stream.hpp"
#include "common/utils.hpp"
#include "hailo/hailort_common.hpp"

namespace hailort
{

static constexpr uint32_t MAX_NMS_BURST_SIZE = 65536;
static const uint64_t NMS_DELIMITER = 0xFFFFFFFFFFFFFFFF;
static const uint64_t NMS_IMAGE_DELIMITER = 0xFFFFFFFFFFFFFFFE;
static const uint64_t NMS_H15_PADDING = 0xFFFFFFFFFFFFFFFD;

enum class NMSBurstState {
    NMS_BURST_STATE_WAITING_FOR_DELIMETER = 0,
    NMS_BURST_STATE_WAITING_FOR_IMAGE_DELIMETER = 1,
    NMS_BURST_STATE_WAITING_FOR_PADDING = 2,
};

class NMSStreamReader {
public:
    static hailo_status read_nms(OutputStream &stream, void *buffer, size_t offset, size_t size);
private:
    static hailo_status read_nms_bbox_mode(OutputStream &stream, void *buffer, size_t offset);
    static hailo_status read_nms_burst_mode(OutputStream &stream, void *buffer, size_t offset, size_t buffer_size);
    static hailo_status advance_state_machine(NMSBurstState *burst_state, const uint64_t current_bbox,
        const hailo_nms_burst_type_t burst_type, const uint32_t num_classes, size_t *num_delimeters_received,
        bool *can_stop_reading_burst, const size_t burst_offset, const size_t burst_size, size_t *burst_index);
};

} /* namespace hailort */

#endif /* _STREAM_INTERNAL_HPP_ */