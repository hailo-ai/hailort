/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file process.cpp
 * @brief Process wrapper for Windows
 **/

#include "common/process.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

namespace hailort
{

Expected<std::pair<int32_t, std::string>> Process::create_and_wait_for_output(const std::string &command_line, uint32_t max_output_size)
{
    // TODO: Add windows impl (HRT-2510)
    command_line;
    max_output_size;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

} /* namespace hailort */
