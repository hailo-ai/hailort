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
const uint32_t HailoRTCommon::MAX_DEFUSED_LAYER_COUNT;
const size_t HailoRTCommon::HW_DATA_ALIGNMENT;
const uint64_t HailoRTCommon::NMS_DELIMITER;
const uint64_t HailoRTCommon::NMS_DUMMY_DELIMITER;

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

} /* namespace hailort */
