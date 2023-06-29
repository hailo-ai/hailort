/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file stream_internal.cpp
 * @brief Implementation of InputStreamBase and OutputStreamBase
 **/

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/transform.hpp"

#include "common/utils.hpp"
#include "common/logger_macros.hpp"

#include "stream_common/stream_internal.hpp"


namespace hailort
{

InputStreamBase::InputStreamBase(const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &core_op_activated_event) :
    m_nn_stream_config(nn_stream_config), m_core_op_activated_event(core_op_activated_event)
{
    m_stream_info = stream_info;
}

hailo_status InputStreamBase::write_async(BufferPtr buffer, const TransferDoneCallback &user_callback)
{
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(buffer->data());
    CHECK(buffer->size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Write size {} must be frame size {}", buffer->size(),
        get_frame_size());

    auto wrapped_callback = [buffer, user_callback](hailo_status status) {
        user_callback(CompletionInfo{status, buffer->data(), buffer->size()});
    };
    return write_async(TransferRequest{MemoryView(*buffer), wrapped_callback, buffer});
}

hailo_status InputStreamBase::write_async(const MemoryView &buffer, const TransferDoneCallback &user_callback)
{
    CHECK_ARG_NOT_NULL(buffer.data());
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Write size {} must be frame size {}", buffer.size(),
        get_frame_size());

    auto wrapped_callback = [buffer, user_callback](hailo_status status) {
        user_callback(CompletionInfo{status, const_cast<uint8_t*>(buffer.data()), buffer.size()});
    };
    return write_async(TransferRequest{buffer, wrapped_callback});
}

hailo_status InputStreamBase::write_async(const void *buffer, size_t size, const TransferDoneCallback &user_callback)
{
    return write_async(MemoryView::create_const(buffer, size), user_callback);
}

hailo_status InputStreamBase::write_async(TransferRequest &&)
{
    LOGGER__ERROR("write_async not implemented for sync API");
    return HAILO_NOT_IMPLEMENTED;
}

EventPtr &InputStreamBase::get_core_op_activated_event()
{
    return m_core_op_activated_event;
}

bool InputStreamBase::is_scheduled()
{
    return false;
}

OutputStreamBase::OutputStreamBase(const LayerInfo &layer_info, const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &core_op_activated_event) :
    m_nn_stream_config(nn_stream_config), m_layer_info(layer_info), m_core_op_activated_event(core_op_activated_event)
{
    m_stream_info = stream_info;
}

hailo_status OutputStreamBase::read_async(BufferPtr buffer, const TransferDoneCallback &user_callback)
{
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(buffer->data());
    CHECK(buffer->size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Read size {} must be frame size {}", buffer->size(),
        get_frame_size());

    auto wrapped_callback = [buffer, user_callback](hailo_status status) {
        user_callback(CompletionInfo{status, const_cast<uint8_t*>(buffer->data()), buffer->size()});
    };
    return read_async(TransferRequest{MemoryView(*buffer), wrapped_callback, buffer});
}

hailo_status OutputStreamBase::read_async(MemoryView buffer, const TransferDoneCallback &user_callback)
{
    CHECK_ARG_NOT_NULL(buffer.data());
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Read size {} must be frame size {}", buffer.size(),
        get_frame_size());

    auto wrapped_callback = [buffer, user_callback](hailo_status status) {
        user_callback(CompletionInfo{status, const_cast<uint8_t*>(buffer.data()), buffer.size()});
    };
    return read_async(TransferRequest{buffer, wrapped_callback});
}

hailo_status OutputStreamBase::read_async(void *buffer, size_t size, const TransferDoneCallback &user_callback)
{
    return read_async(MemoryView(buffer, size), user_callback);
}

hailo_status OutputStreamBase::read_async(TransferRequest &&)
{
    LOGGER__ERROR("read_async not implemented for sync API");
    return HAILO_NOT_IMPLEMENTED;
}

EventPtr &OutputStreamBase::get_core_op_activated_event()
{
    return m_core_op_activated_event;
}

bool OutputStreamBase::is_scheduled()
{
    return false;
}

} /* namespace hailort */
