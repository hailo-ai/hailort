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
#include "stream_common/nms_stream_reader.hpp"

#include <sstream>

namespace hailort
{

hailo_status InputStream::flush()
{
    return HAILO_SUCCESS;
}

hailo_status InputStream::write(const MemoryView &buffer)
{
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT,
        "write size {} must be {}", buffer.size(), get_frame_size());

    CHECK(((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0), HAILO_INVALID_ARGUMENT,
        "Input must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());

    return write_impl(buffer);
}

hailo_status InputStream::write(const void *buffer, size_t size)
{
    return write(MemoryView::create_const(buffer, size));
}

hailo_status InputStream::wait_for_async_ready(size_t /* transfer_size */, std::chrono::milliseconds /* timeout */)
{
    LOGGER__ERROR("wait_for_async_ready not implemented for sync API");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<size_t> InputStream::get_async_max_queue_size() const
{
    LOGGER__ERROR("get_async_max_queue_size not implemented for sync API");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

std::string InputStream::to_string() const
{
    std::stringstream string_stream;
    string_stream << "InputStream(index=" << static_cast<uint32_t>(get_info().index)
                    << ", name=" << get_info().name << ")";
    return string_stream.str();
}

EventPtr &InputStream::get_network_group_activated_event()
{
    LOGGER__WARNING("VDevice InputStream::get_network_group_activated_event() is deprecated.");
    return get_core_op_activated_event();
}

hailo_status OutputStream::read_nms(void *buffer, size_t offset, size_t size)
{
    CHECK(size == get_info().hw_frame_size, HAILO_INSUFFICIENT_BUFFER,
        "On nms stream buffer size should be {} (given size {})", get_info().hw_frame_size, size);

    return NMSStreamReader::read_nms((*this), buffer, offset, size);
}

hailo_status OutputStream::read(MemoryView buffer)
{
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Read size {} must be {}", buffer.size(),
        get_frame_size());

    if (get_info().format.order == HAILO_FORMAT_ORDER_HAILO_NMS){
        return read_nms(buffer.data(), 0, buffer.size());
    } else {
        return read_impl(buffer);
    }
}

hailo_status OutputStream::read(void *buffer, size_t size)
{
    return read(MemoryView(buffer, size));
}

hailo_status OutputStream::wait_for_async_ready(size_t /* transfer_size */, std::chrono::milliseconds /* timeout */)
{
    LOGGER__ERROR("wait_for_async_ready not implemented for sync API");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<size_t> OutputStream::get_async_max_queue_size() const
{
    LOGGER__ERROR("get_async_max_queue_size not implemented for sync API");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

std::string OutputStream::to_string() const
{
    std::stringstream string_stream;
    string_stream << "OutputStream(index=" << static_cast<uint32_t>(get_info().index)
                    << ", name=" << get_info().name << ")";
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

EventPtr &OutputStream::get_network_group_activated_event()
{
    LOGGER__WARNING("VDevice OutputStream::get_network_group_activated_event() is deprecated.");
    return get_core_op_activated_event();
}


} /* namespace hailort */
