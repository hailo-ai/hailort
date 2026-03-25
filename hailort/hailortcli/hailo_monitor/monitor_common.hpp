/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file monitor_common.hpp
 * @brief Common utilities shared between accelerator and integrated monitors
 **/

#ifndef _HAILO_MONITOR_COMMON_HPP_
#define _HAILO_MONITOR_COMMON_HPP_

#include "hailo/hailort.h"

#include <signal.h>

namespace hailort
{

class InterruptHandler
{
public:
    static void register_handler();
    static bool is_running() { return m_is_running; }

private:
    static void sigint_handler(int);
    static volatile bool m_is_running;
};

} /* namespace hailort */

#endif /* _HAILO_MONITOR_COMMON_HPP_ */
