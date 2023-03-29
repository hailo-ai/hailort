/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file scheduler_mon.hpp
 * @brief Defines for scheduler monitor of networks.
 **/

#ifndef _HAILO_SCHEDULER_MON_HPP_
#define _HAILO_SCHEDULER_MON_HPP_

#include "hailo/hailort.h"

#include "common/filesystem.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "scheduler_mon.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop ) 
#else
#pragma GCC diagnostic pop
#endif

#include <iostream>
#include <string>


namespace hailort
{

#define SCHEDULER_MON_TMP_DIR ("/tmp/hmon_files/")
#define SCHEDULER_MON_ENV_VAR ("HAILO_MONITOR")
#define DEFAULT_SCHEDULER_MON_INTERVAL (std::chrono::seconds(1))
#define SCHEDULER_MON_NAN_VAL (-1)

class SchedulerMon
{
public:

    static bool should_monitor()
    {
    #if defined(__GNUC__)
        auto mon_var = std::getenv(SCHEDULER_MON_ENV_VAR);
        return (mon_var != nullptr) && strncmp(mon_var, "1", 1) == 0;
    #else
        // TODO: HRT-7304 - Add support for windows
        return false;
    #endif
    }
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_MON_HPP_ */
