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
#include "hailo/network_group.hpp"
#include "common/utils.hpp"
#include "network_group_scheduler.hpp"

namespace hailort
{

class NetworkGroupSchedulerOracle
{
public:
    static bool choose_next_model(NetworkGroupScheduler &scheduler, uint32_t device_id);
    static uint32_t get_avail_device(NetworkGroupScheduler &scheduler, scheduler_ng_handle_t network_group_handle);

private:
    NetworkGroupSchedulerOracle() {}
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_ORACLE_HPP_ */
