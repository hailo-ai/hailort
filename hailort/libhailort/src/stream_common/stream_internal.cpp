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
    return write_async(TransferRequest(std::move(buffer), wrapped_callback));
}

hailo_status InputStreamBase::write_async(const MemoryView &buffer, const TransferDoneCallback &user_callback)
{
    CHECK(0 == (reinterpret_cast<size_t>(buffer.data()) % HailoRTCommon::HW_DATA_ALIGNMENT), HAILO_INVALID_ARGUMENT,
        "User address must be aligned to {}", HailoRTCommon::HW_DATA_ALIGNMENT);

    const auto dma_able_alignment = OsUtils::get_dma_able_alignment();
    // User address is not aligned to page size
    if ((0 != (reinterpret_cast<size_t>(buffer.data()) % dma_able_alignment))) {
        auto user_buffer = UserBufferStorage::create_storage_from_user_buffer(const_cast<uint8_t*>(buffer.data()), buffer.size());
        CHECK_EXPECTED_AS_STATUS(user_buffer);
        return write_async(user_buffer.release(), user_callback);
    } else {
        auto dma_able_buffer = DmaStorage::create_dma_able_buffer_from_user_size(const_cast<uint8_t*>(buffer.data()), buffer.size());
        CHECK_EXPECTED_AS_STATUS(dma_able_buffer);
        return write_async(dma_able_buffer.release(), user_callback);
    }
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

hailo_status InputStreamBase::abort()
{
    LOGGER__ERROR("InputStream::abort is deprecated. One should use ConfiguredNetworkGroup::shutdown()");
    return abort_impl();
}

hailo_status InputStreamBase::clear_abort()
{
    LOGGER__ERROR("InputStream::clear_abort() is deprecated. To reuse network after shutdown, reconfigure it");
    return clear_abort_impl();
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
void InputStreamBase::set_vdevice_core_op_handle(vdevice_core_op_handle_t /*core_op_handle*/) {}

hailo_status InputStreamBase::cancel_pending_transfers()
{
    LOGGER__ERROR("cancel_pending_transfers not implemented for this type of stream");
    return HAILO_NOT_IMPLEMENTED;
}

OutputStreamBase::OutputStreamBase(const LayerInfo &layer_info, const hailo_stream_info_t &stream_info,
        const EventPtr &core_op_activated_event) :
    m_layer_info(layer_info), m_core_op_activated_event(core_op_activated_event)
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
    return read_async(TransferRequest(std::move(buffer), wrapped_callback));
}

hailo_status OutputStreamBase::read_async(MemoryView buffer, const TransferDoneCallback &user_callback)
{
    CHECK_ARG_NOT_NULL(buffer.data());
    CHECK(buffer.size() == get_frame_size(), HAILO_INVALID_ARGUMENT, "Read size {} must be frame size {}", buffer.size(),
        get_frame_size());

    const auto dma_able_alignment = HailoRTCommon::DMA_ABLE_ALIGNMENT_READ_HW_LIMITATION;
    BufferPtr wrapped_buffer = nullptr;
    if ((0 != (reinterpret_cast<size_t>(buffer.data()) % dma_able_alignment))) {
        auto user_buffer = UserBufferStorage::create_storage_from_user_buffer(const_cast<uint8_t*>(buffer.data()), buffer.size());
        CHECK_EXPECTED_AS_STATUS(user_buffer);
        wrapped_buffer = user_buffer.release();
    } else {
        auto dma_able_buffer = DmaStorage::create_dma_able_buffer_from_user_size(const_cast<uint8_t*>(buffer.data()), buffer.size());
        CHECK_EXPECTED_AS_STATUS(dma_able_buffer);
        wrapped_buffer = dma_able_buffer.release();
    }

    return read_async(wrapped_buffer, user_callback);
}

hailo_status OutputStreamBase::read_async(void *buffer, size_t size, const TransferDoneCallback &user_callback)
{
    return read_async(MemoryView(buffer, size), user_callback);
}

hailo_status OutputStreamBase::read_unaligned_address_async(const MemoryView &, const TransferDoneCallback &)
{
    LOGGER__ERROR("read_unaligned_address_async not implemented OutputStreamBase");
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status OutputStreamBase::read_async(TransferRequest &&)
{
    LOGGER__ERROR("read_async not implemented for sync API");
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status OutputStreamBase::abort()
{
    LOGGER__ERROR("OutputStream::abort is deprecated. One should use ConfiguredNetworkGroup::shutdown()");
    return abort_impl();
}

hailo_status OutputStreamBase::clear_abort()
{
    LOGGER__ERROR("OutputStream::clear_abort() is deprecated. To reuse network after shutdown, reconfigure it");
    return clear_abort_impl();
}

EventPtr &OutputStreamBase::get_core_op_activated_event()
{
    return m_core_op_activated_event;
}

bool OutputStreamBase::is_scheduled()
{
    return false;
}

// TODO - HRT-11739 - remove vdevice related members/functions (get/set_vdevice_core_op_handle)
void OutputStreamBase::set_vdevice_core_op_handle(vdevice_core_op_handle_t) {}

hailo_status OutputStreamBase::cancel_pending_transfers()
{
    LOGGER__ERROR("cancel_pending_transfers not implemented for this type of stream");
    return HAILO_NOT_IMPLEMENTED;
}

} /* namespace hailort */
