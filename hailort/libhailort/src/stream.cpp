/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file stream.cpp
 * @brief Implementation of stream abstraction
 **/

#include "hailo/stream.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/transform.hpp"
#include "common/utils.hpp"
#include "hef_internal.hpp"
#include "microprofile.h"

#include <sstream>

namespace hailort
{

hailo_status InputStream::flush()
{
    return HAILO_SUCCESS;
}

hailo_status InputStream::write(const MemoryView &buffer)
{
    MICROPROFILE_SCOPEI("Stream", "Write", 0);
    CHECK((buffer.size() % m_stream_info.hw_frame_size) == 0, HAILO_INVALID_ARGUMENT,
        "write size {} must be a multiple of hw size {}", buffer.size(), m_stream_info.hw_frame_size);

    CHECK(((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0), HAILO_INVALID_ARGUMENT,
        "Input must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());
    
    auto status = sync_write_all_raw_buffer_no_transform_impl(const_cast<uint8_t*>(buffer.data()), 0, buffer.size());
    MicroProfileFlip(nullptr);
    return status;
}

std::string InputStream::to_string() const
{
    std::stringstream string_stream;
    string_stream << "InputStream(index=" << static_cast<uint32_t>(m_stream_info.index)
                    << ", name=" << m_stream_info.name << ")";
    return string_stream.str();
}

OutputStream::OutputStream(OutputStream &&other) : m_stream_info(std::move(other.m_stream_info)),
    m_dataflow_manager_id(std::move(other.m_dataflow_manager_id)),
    m_invalid_frames_count(static_cast<uint32_t>(other.m_invalid_frames_count))
{}

hailo_status OutputStream::read_nms(void *buffer, size_t offset, size_t size)
{
    uint32_t num_of_classes = m_stream_info.nms_info.number_of_classes;
    uint32_t max_bboxes_per_class = m_stream_info.nms_info.max_bboxes_per_class;
    uint32_t chunks_per_frame = m_stream_info.nms_info.chunks_per_frame;
    size_t bbox_size = m_stream_info.nms_info.bbox_size;
    size_t transfer_size = bbox_size;

    CHECK(size == m_stream_info.hw_frame_size, HAILO_INSUFFICIENT_BUFFER,
        "On nms stream buffer size should be {} (given size {})", m_stream_info.hw_frame_size, size);

    for (uint32_t chunk_index = 0; chunk_index < chunks_per_frame; chunk_index++) {
        for (uint32_t class_index = 0; class_index < num_of_classes; class_index++) {
            nms_bbox_counter_t class_bboxes_count = 0;
            nms_bbox_counter_t* class_bboxes_count_ptr = (nms_bbox_counter_t*)(reinterpret_cast<uint8_t*>(buffer) + offset);
            offset += sizeof(*class_bboxes_count_ptr);

            // Read bboxes until reaching delimiter
            for (;;) {
                MemoryView buffer_view(static_cast<uint8_t*>(buffer) + offset, transfer_size);
                auto expected_bytes_read = sync_read_raw_buffer(buffer_view);
                if ((HAILO_STREAM_INTERNAL_ABORT == expected_bytes_read.status()) ||
                    ((HAILO_STREAM_NOT_ACTIVATED == expected_bytes_read.status()))) {
                    return expected_bytes_read.status();
                }
                CHECK_EXPECTED_AS_STATUS(expected_bytes_read, "Failed reading nms bbox");
                transfer_size = expected_bytes_read.release();
                CHECK(transfer_size == bbox_size, HAILO_INTERNAL_FAILURE,
                    "Data read from the device was size {}, should be bbox size {}", transfer_size, bbox_size);

                if (HailoRTCommon::NMS_DUMMY_DELIMITER == *(uint64_t*)((uint8_t*)buffer + offset)) {
                    continue;
                }

                if (HailoRTCommon::NMS_DELIMITER == *(uint64_t*)((uint8_t*)buffer + offset)) {
                    break;
                }

                class_bboxes_count++;
                CHECK(class_bboxes_count <= max_bboxes_per_class, HAILO_INTERNAL_FAILURE,
                    "Data read from the device for the current class was size {}, max size is {}", class_bboxes_count, max_bboxes_per_class);
                offset += bbox_size;
            }

            *class_bboxes_count_ptr = class_bboxes_count;
        }
    }
    return HAILO_SUCCESS;
}

hailo_status OutputStream::read(MemoryView buffer)
{
    MICROPROFILE_SCOPEI("Stream", "Read", 0);
    CHECK((buffer.size() % m_stream_info.hw_frame_size) == 0, HAILO_INVALID_ARGUMENT,
        "When read size {} must be a multiple of hw size {}", buffer.size(), m_stream_info.hw_frame_size);

    if (m_stream_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS){
        return read_nms(buffer.data(), 0, buffer.size());
    } else {
        return this->read_all(buffer);
    }
}

std::string OutputStream::to_string() const
{
    std::stringstream string_stream;
    string_stream << "OutputStream(index=" << static_cast<uint32_t>(m_stream_info.index)
                    << ", name=" << m_stream_info.name << ")";
    return string_stream.str();
}

uint32_t OutputStream::get_invalid_frames_count() const
{
    return m_invalid_frames_count.load();
}

void OutputStream::increase_invalid_frames_count(uint32_t value)
{
    m_invalid_frames_count = m_invalid_frames_count + value;
}


} /* namespace hailort */
