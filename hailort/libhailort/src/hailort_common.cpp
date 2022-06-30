/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_common.cpp
 * @brief Implementation of common hailort utilities
 **/

#include "hailo/hailort_common.hpp"

namespace hailort
{

// Needed for the linker
const uint32_t HailoRTCommon::BBOX_PARAMS;
const uint32_t HailoRTCommon::MAX_DEFUSED_LAYER_COUNT;
const size_t HailoRTCommon::HW_DATA_ALIGNMENT;
const uint64_t HailoRTCommon::NMS_DELIMITER;
const uint64_t HailoRTCommon::NMS_DUMMY_DELIMITER;

} /* namespace hailort */
