/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include <sstream>

namespace hailort
{

hailo_status InputStream::flush()
{
    return HAILO_SUCCESS;
}

hailo_status InputStream::wait_for_async_ready(size_t /* transfer_size */, std::chrono::milliseconds /* timeout */)
{
    LOGGER__ERROR("wait_for_async_ready not implemented for sync API");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<size_t> InputStream::get_async_max_queue_size() const
{
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

hailo_status OutputStream::wait_for_async_ready(size_t /* transfer_size */, std::chrono::milliseconds /* timeout */)
{
    LOGGER__ERROR("wait_for_async_ready not implemented for sync API");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<size_t> OutputStream::get_async_max_queue_size() const
{
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
