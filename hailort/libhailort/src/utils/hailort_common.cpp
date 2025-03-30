/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_common.cpp
 * @brief Implementation of common hailort utilities
 **/

#include "hailo/hailort_common.hpp"
#include "common/utils.hpp"

namespace hailort
{

// Needed for the linker
const uint32_t HailoRTCommon::BBOX_PARAMS;
const uint32_t HailoRTCommon::MAX_DEFUSED_LAYER_COUNT;
const size_t HailoRTCommon::HW_DATA_ALIGNMENT;
const uint32_t HailoRTCommon::MAX_NMS_BURST_SIZE;
const size_t HailoRTCommon::DMA_ABLE_ALIGNMENT_WRITE_HW_LIMITATION;
const size_t HailoRTCommon::DMA_ABLE_ALIGNMENT_READ_HW_LIMITATION;

// TODO: HRT-15885 - remove this function
uint32_t HailoRTCommon::get_nms_host_shape_size(const hailo_nms_info_t &nms_info)
{
    LOGGER__WARNING("get_nms_host_shape_size is deprecated, use get_nms_by_class_host_shape_size instead");
    return get_nms_by_class_host_shape_size(nms_info);
}

// TODO: HRT-15885 - remove this function
uint32_t HailoRTCommon::get_nms_host_shape_size(const hailo_nms_shape_t &nms_shape)
{
    LOGGER__WARNING("get_nms_host_shape_size is deprecated, use get_nms_by_class_host_shape_size instead");
    return get_nms_by_class_host_shape_size(nms_shape);
}

Expected<hailo_device_id_t> HailoRTCommon::to_device_id(const std::string &device_id)
{
    hailo_device_id_t id = {};
    static constexpr size_t id_size = ARRAY_ENTRIES(id.id);

    CHECK_AS_EXPECTED(device_id.size() < id_size, HAILO_INTERNAL_FAILURE,
        "Device '{}' has a too long id (max is {})", device_id, id_size);

    strncpy(id.id, device_id.c_str(), id_size - 1);
    id.id[id_size - 1] = 0;
    return id;
}

Expected<std::vector<hailo_device_id_t>> HailoRTCommon::to_device_ids_vector(const std::vector<std::string> &device_ids_str)
{
    std::vector<hailo_device_id_t> device_ids_vector;
    device_ids_vector.reserve(device_ids_str.size());
    for (const auto &device_id_str : device_ids_str) {
        auto device_id_struct = to_device_id(device_id_str);
        CHECK_EXPECTED(device_id_struct);
        device_ids_vector.push_back(device_id_struct.release());
    }
    return device_ids_vector;
}

uint32_t HailoRTCommon::get_nms_host_frame_size(const hailo_nms_shape_t &nms_shape, const hailo_format_t &format)
{
    double frame_size = 0;
    if (HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK == format.order) {
        frame_size = get_nms_with_byte_mask_host_frame_size(nms_shape);
    } else if (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE == format.order) {
        frame_size = get_nms_by_score_host_frame_size(nms_shape);
    } else {
        auto shape_size = get_nms_by_class_host_shape_size(nms_shape);
        frame_size =  shape_size * get_format_data_bytes(format);
    }
    if (frame_size < UINT32_MAX) {
        return static_cast<uint32_t>(frame_size);
    } else{
        LOGGER__WARNING("NMS host frame size calculated is larger then UINT32_MAX. Therefore the frame size is UINT32_MAX");
        return UINT32_MAX;
    }
}

Expected<hailo_pix_buffer_t> HailoRTCommon::as_hailo_pix_buffer(MemoryView memory_view, hailo_format_order_t order)
{
    switch(order){
    case HAILO_FORMAT_ORDER_NV12:
    case HAILO_FORMAT_ORDER_NV21: {
        CHECK_AS_EXPECTED(0 == (memory_view.size() % 3), HAILO_INVALID_ARGUMENT, "buffer size must be divisible by 3");
        auto y_plane_size = memory_view.size() * 2 / 3;
        auto uv_plane_size = memory_view.size() * 1 / 3;

        auto uv_data_ptr = reinterpret_cast<uint8_t*>(memory_view.data()) + y_plane_size;

        hailo_pix_buffer_plane_t y {uint32_t(y_plane_size), uint32_t(y_plane_size), {memory_view.data()}};
        hailo_pix_buffer_plane_t uv {uint32_t(uv_plane_size), uint32_t(uv_plane_size), {uv_data_ptr}};
        // Currently only support HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR
        hailo_pix_buffer_t buffer{0, {y, uv}, NUMBER_OF_PLANES_NV12_NV21, HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR};

        return buffer;
    }
    case HAILO_FORMAT_ORDER_I420: {
        CHECK_AS_EXPECTED(0 == (memory_view.size() % 6), HAILO_INVALID_ARGUMENT, "buffer size must be divisible by 6");

        auto y_plane_size = memory_view.size() * 2 / 3;
        auto u_plane_size = memory_view.size() * 1 / 6;
        auto v_plane_size = memory_view.size() * 1 / 6;

        auto u_data_ptr = (char*)memory_view.data() + y_plane_size;
        auto v_data_ptr = u_data_ptr + u_plane_size;

        hailo_pix_buffer_plane_t y {uint32_t(y_plane_size), uint32_t(y_plane_size), {memory_view.data()}};
        hailo_pix_buffer_plane_t u {uint32_t(u_plane_size), uint32_t(u_plane_size), {u_data_ptr}};
        hailo_pix_buffer_plane_t v {uint32_t(v_plane_size), uint32_t(v_plane_size), {v_data_ptr}};
        // Currently only support HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR
        hailo_pix_buffer_t buffer{0, {y, u, v}, NUMBER_OF_PLANES_I420, HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR};

        return buffer;
    }
    default: {
        hailo_pix_buffer_plane_t plane = {(uint32_t)memory_view.size(), (uint32_t)memory_view.size(), {memory_view.data()}};
        // Currently only support HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR
        hailo_pix_buffer_t buffer{0, {plane}, 1, HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR};
        return buffer;
    }
    }
}

} /* namespace hailort */
