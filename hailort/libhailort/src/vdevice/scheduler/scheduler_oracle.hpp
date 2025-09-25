/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

struct RunParams {
    scheduler_core_op_handle_t core_op_handle;
    device_id_t device_id;
};

class CoreOpsSchedulerOracle
{
public:
    static scheduler_core_op_handle_t choose_next_model(SchedulerBase &scheduler, const device_id_t &device_id, bool check_threshold);
    static std::vector<RunParams> get_oracle_decisions(SchedulerBase &scheduler);
    static bool should_stop_streaming(SchedulerBase &scheduler, core_op_priority_t core_op_priority, const device_id_t &device_id);

private:
    CoreOpsSchedulerOracle() {}
    // TODO: Consider returning a vector of devices (we can use this function in other places)
    static bool is_core_op_active(SchedulerBase &scheduler, scheduler_core_op_handle_t core_op_handle);
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_ORACLE_HPP_ */
