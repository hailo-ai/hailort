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
#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"

namespace hailort
{
namespace genai
{

// Forward declarations
class SessionWrapper;

class GenAICommon
{
public:
    GenAICommon() = delete;

    static Expected<std::shared_ptr<SessionWrapper>> create_session_wrapper(
        const hailo_vdevice_params_t &vdevice_params, uint16_t connection_port);

private:
    static hailo_status validate_genai_vdevice_params(const hailo_vdevice_params_t &vdevice_params);
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_COMMON_INTERNAL_HPP_ */
