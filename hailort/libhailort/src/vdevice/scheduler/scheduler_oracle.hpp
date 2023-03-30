/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_oracle.hpp
 * @brief
 **/

#ifndef _HAILO_SCHEDULER_ORACLE_HPP_
#define _HAILO_SCHEDULER_ORACLE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/utils.hpp"

#include "vdevice/scheduler/scheduler_base.hpp"


namespace hailort
{

class CoreOpsSchedulerOracle
{
public:
    static bool choose_next_model(SchedulerBase &scheduler, uint32_t device_id, bool check_threshold);
    static uint32_t get_avail_device(SchedulerBase &scheduler, scheduler_core_op_handle_t core_op_handle);
    static bool should_stop_streaming(SchedulerBase &scheduler, core_op_priority_t core_op_priority);

private:
    CoreOpsSchedulerOracle() {}
    // TODO: Consider returning a vector of devices (we can use this function in other places)
    static bool is_core_op_active(SchedulerBase &scheduler, scheduler_core_op_handle_t core_op_handle);
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_ORACLE_HPP_ */
