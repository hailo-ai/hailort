/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_common.cpp
 * @brief Common functions for GenAI
 **/

#include "genai_common.hpp"

namespace hailort
{
namespace genai
{

hailo_status GenAICommon::validate_genai_vdevice_params(const hailo_vdevice_params_t &vdevice_params)
{
    CHECK(vdevice_params.device_count == 1, HAILO_INVALID_ARGUMENT, "Only single physical device is supported for GenAI!");
    CHECK(vdevice_params.multi_process_service == false, HAILO_NOT_SUPPORTED, "Working with multi-process service is not supported for GenAI");
    CHECK(vdevice_params.scheduling_algorithm != HAILO_SCHEDULING_ALGORITHM_NONE, HAILO_NOT_SUPPORTED,
        "Working without scheduler is not supported for GenAI");

    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
