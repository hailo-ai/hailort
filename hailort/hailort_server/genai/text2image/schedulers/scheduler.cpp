/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler.cpp
 * @brief Diffusion Scheduler
 **/

#ifndef _HAILO_GENAI_SCHEDULER_HPP_
#define _HAILO_GENAI_SCHEDULER_HPP_

#include "scheduler.hpp"
#include "euler_discrete.hpp"
#include "ddim.hpp"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<Scheduler>> Scheduler::create(HailoDiffuserSchedulerType scheduler_type)
{
    switch (scheduler_type) {
        case HailoDiffuserSchedulerType::EULER_DISCRETE:
        {
            TRY(auto euler_discrete_scheduler, EulerDiscreteScheduler::create());
            return std::unique_ptr<Scheduler>(std::move(euler_discrete_scheduler));
        }
        case HailoDiffuserSchedulerType::DDIM:
        {
            TRY(auto ddim_scheduler, DDIMScheduler::create());
            return std::unique_ptr<Scheduler>(std::move(ddim_scheduler));
        }
        default:
        {
            LOGGER__ERROR("Unsupported scheduler type {}", static_cast<int>(scheduler_type));
            return make_unexpected(HAILO_NOT_SUPPORTED);
        }
    }
}

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_SCHEDULER_HPP_ */
