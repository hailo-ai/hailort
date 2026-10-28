/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file microsec_timer.cpp
 * @brief Timer wrapper for Linux
 **/

#include "os/microsec_timer.hpp"
#include <thread>
#include <chrono>

namespace hailort
{

void MicrosecTimer::sleep(uint64_t microsecs)
{
    std::this_thread::sleep_for(std::chrono::microseconds(microsecs));
}

} /* namespace hailort */
