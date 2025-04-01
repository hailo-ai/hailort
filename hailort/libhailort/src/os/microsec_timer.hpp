/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file timer.hpp
 * @brief High resolution (microsec granularity) timer.
 **/

#ifndef __OS_MICROSEC_TIMER_HPP__
#define __OS_MICROSEC_TIMER_HPP__

#include <hailo/platform.h>
#include <hailo/hailort.h>
#include "common/utils.hpp"

namespace hailort
{

class MicrosecTimer final
{
public:
    MicrosecTimer() = delete;

    /**
     * Sleeps for the desired time
     * 
     * @param[in] microsecs    The desired sleep period
     * @note Passing 0 as a parameter will cause a context switch
     * @note Some implmentions of sleep may go into a busy-loop if the platform (os and processor)
     *       doesn't support such short periods.
     * @note This function is guaranteed to sleep for at least the desired time, though it may sleep for more.
     */
    static void sleep(uint64_t microsecs);

    static void sleep(std::chrono::microseconds microsecs)
    {
        sleep(microsecs.count());
    }
};

} /* namespace hailort */

#endif /* __OS_MICROSEC_TIMER_HPP__ */
