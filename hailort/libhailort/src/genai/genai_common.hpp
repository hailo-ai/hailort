/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file common_internal.hpp
 * @brief TODO: fill me
 **/

#ifndef _HAILO_COMMON_INTERNAL_HPP_
#define _HAILO_COMMON_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/genai/common.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

namespace hailort
{
namespace genai
{

class GenAICommon
{
public:
    GenAICommon() = delete;

    static hailo_status validate_genai_vdevice_params(const hailo_vdevice_params_t &vdevice_params);
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_COMMON_INTERNAL_HPP_ */
