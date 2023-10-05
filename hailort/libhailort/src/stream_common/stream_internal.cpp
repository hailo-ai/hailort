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
#include "common/os_utils.hpp"

#include "stream_common/stream_internal.hpp"


namespace hailort
{

static Expected<BufferPtr> create_dma_able_buffer_from_user_size(void *addr, size_t size)
{
    auto storage = DmaStorage::create_from_user_address(addr, size);
    CHECK_EXPECTED(storage);

    auto buffer = make_shared_nothrow<Buffer>(storage.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer, HAILO_OUT_OF_HOST_MEMORY);

    return buffer;
}

hailo_status InputStreamBase::write(const MemoryView &buffer)
{
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT,
        "write size {} must be {}", buffer.size(), get_frame_size());

    CHECK(((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0), HAILO_INVALID_ARGUMENT,
        "Input must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());

    return write_impl(buffer);
}

hailo_status InputStreamBase::write(const void *buffer, size_t size)
{
    return write(MemoryView::create_const(buffer, size));
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
    return write_async(TransferRequest{ buffer, wrapped_callback});
}

hailo_status InputStreamBase::write_async(const MemoryView &buffer, const TransferDoneCallback &user_callback)
{
    auto dma_able_buffer = create_dma_able_buffer_from_user_size(const_cast<uint8_t*>(buffer.data()), buffer.size());
    CHECK_EXPECTED_AS_STATUS(dma_able_buffer);

    return write_async(dma_able_buffer.release(), user_callback);
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

// TODO - HRT-11739 - remove vdevice related members/functions (get/set_vdevice_core_op_handle)
vdevice_core_op_handle_t InputStreamBase::get_vdevice_core_op_handle()
{
    LOGGER__WARNING("VDevice InputStream::get_vedvice_core_op_handle is not implemented for this class.");
    return INVALID_CORE_OP_HANDLE;
}

void InputStreamBase::set_vdevice_core_op_handle(vdevice_core_op_handle_t /*core_op_handle*/)
{
    LOGGER__WARNING("VDevice InputStream::set_vedvice_core_op_handle is not implemented for this class.");
}

OutputStreamBase::OutputStreamBase(const LayerInfo &layer_info, const hailo_stream_info_t &stream_info,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, const EventPtr &core_op_activated_event) :
    m_nn_stream_config(nn_stream_config), m_layer_info(layer_info), m_core_op_activated_event(core_op_activated_event)
{
    m_stream_info = stream_info;
    m_quant_infos = m_layer_info.quant_infos;
}

hailo_status OutputStreamBase::read(MemoryView buffer)
{
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Read size {} must be {}", buffer.size(),
        get_frame_size());

    return read_impl(buffer);
}

hailo_status OutputStreamBase::read(void *buffer, size_t size)
{
    return read(MemoryView(buffer, size));
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
    return read_async(TransferRequest{buffer, wrapped_callback});
}

hailo_status OutputStreamBase::read_async(MemoryView buffer, const TransferDoneCallback &user_callback)
{
    CHECK_ARG_NOT_NULL(buffer.data());
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Read size {} must be frame size {}", buffer.size(),
        get_frame_size());

    auto wrapped_callback = [buffer, user_callback](hailo_status status) {
        user_callback(CompletionInfo{status, const_cast<uint8_t*>(buffer.data()), buffer.size()});
    };

    auto dma_able_buffer = create_dma_able_buffer_from_user_size(buffer.data(), buffer.size());
    CHECK_EXPECTED_AS_STATUS(dma_able_buffer);

    return read_async(dma_able_buffer.release(), user_callback);
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
