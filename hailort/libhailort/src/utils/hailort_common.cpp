/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
const uint32_t HailoRTCommon::MASK_PARAMS;
const uint32_t HailoRTCommon::MAX_DEFUSED_LAYER_COUNT;
const size_t HailoRTCommon::HW_DATA_ALIGNMENT;
const uint32_t HailoRTCommon::MAX_NMS_BURST_SIZE;
const size_t HailoRTCommon::DMA_ABLE_ALIGNMENT_WRITE_HW_LIMITATION;
const size_t HailoRTCommon::DMA_ABLE_ALIGNMENT_READ_HW_LIMITATION;

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
    auto shape_size = 0;
    if (HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK == format.order) {
        shape_size = get_nms_with_byte_mask_host_shape_size(nms_shape, format);
    } else {
        shape_size = get_nms_host_shape_size(nms_shape);
    }
    double frame_size = shape_size * get_format_data_bytes(format);
    if (frame_size < UINT32_MAX) {
        return static_cast<uint32_t>(frame_size);
    } else{
        LOGGER__WARNING("NMS host frame size calculated is larger then UINT32_MAX. Therefore the frame size is UINT32_MAX");
        return UINT32_MAX;
    }
}

} /* namespace hailort */
