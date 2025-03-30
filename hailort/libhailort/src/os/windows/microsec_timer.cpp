/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file timer.cpp
 * @brief Timer wrapper for Windows
 **/

#include "os/microsec_timer.hpp"
#include <Windows.h>
#include <thread>
#include <chrono>

namespace hailort
{

// TODO: QueryPerformanceFrequency and QueryPerformanceCounter can't fail on systems
//       running Windows XP or later, hence we ignore the return value. Do we want to support pre xp?

static uint64_t query_frequency()
{
    // From MSDN QueryPerformanceFrequency:
    // The frequency of the performance counter is fixed at system boot and is consistent across all processors.
    // Therefore, the frequency need only be queried upon application initialization, and the result can be cached.
    static LARGE_INTEGER frequency{};
    if (frequency.QuadPart != 0) {
        // The function has already been called
        return frequency.QuadPart;
    }
    (void)QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}

// Note: This function will cause a busy-loop for a parameter of microsecs <= 1000.
//       This is because Windows' Sleep only works in millisec granulrity.
//       An iteresting alternative is to use SetWaitableTimer, which is supposed to work
//       in 100 nanosec (=0.1 microsec) granulrity. However, our testing showed that the
//       precision of SetWaitableTimer wasn't good enougth for the sleeps required by eth_stream's
//       token_bucket (less than 10 microsecs for certian bandwidths).
void MicrosecTimer::sleep(uint64_t microsecs)
{
    static const uint64_t MINIMUM_SLEEP_TIME_USEC = 1000;
    static const uint64_t frequency = query_frequency();

    if (microsecs > MINIMUM_SLEEP_TIME_USEC || microsecs == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(microsecs));
    } else {
        LARGE_INTEGER starting_time{};
        LARGE_INTEGER ending_time{};
        LARGE_INTEGER elapsed_microseconds{};

        (void)QueryPerformanceCounter(&starting_time);
        // Note: elapsed_microseconds.QuadPart will always be non-negative, hence the cast is ok.
        // It will be non-negative because it's either zero (the initial value) or ending_time - starting_time.
        // Since ending_time is set from a call to QueryPerformanceCounter the occours after the first call to
        // QueryPerformanceCounter in which starting_time is set, ending_time will be greater than starting time.
        while (static_cast<uint64_t>(elapsed_microseconds.QuadPart) < microsecs) {
            (void)QueryPerformanceCounter(&ending_time);
            elapsed_microseconds.QuadPart = ending_time.QuadPart - starting_time.QuadPart;
            elapsed_microseconds.QuadPart *= 1000000;
            elapsed_microseconds.QuadPart /= frequency;
            // Note: This will relinquish the remainder of the thread's time slice. 
            //       Testing proved that this sleep didn't harm the performance of the token_bucket.
            //       This sleep is here for consistency with the documention. 
            std::this_thread::sleep_for(std::chrono::microseconds(0));
        }
    }
}

} /* namespace hailort */
