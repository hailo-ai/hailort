/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler.hpp
 * @brief Diffusion Scheduler - definitions + interface
 *
 *  Inspired by https://github.com/openvinotoolkit/openvino.genai/blob/f274cb33f4b05332a93230fd3f81aa875410fb1a/src/cpp/src/image_generation/schedulers/ischeduler.hpp
 *  (Apache License 2.0)
 **/

#ifndef _HAILO_SCHEDULER_HPP_
#define _HAILO_SCHEDULER_HPP_

#include "hailo/hailort.h"
#include "hailo/genai/text2image/text2image.hpp"
#include "common/utils.hpp"


namespace hailort
{
namespace genai
{

enum class BetaSchedule {
    LINEAR = 0,
    SCALED_LINEAR,
    SQUAREDCOS_CAP_V2
};

enum class PredictionType {
    EPSILON = 0,
    SAMPLE,
    V_PREDICTION
};

enum class TimestepSpacing {
    LINSPACE = 0,
    TRAILING,
    LEADING
};

enum class TimestepType {
    DISCRETE = 0,
    CONTINUOUS
};

enum class InterpolationType {
    LINEAR = 0,
    LOG_LINEAR,
};

enum class FinalSigmaType {
    ZERO = 0,
    SIGMA_MIN
};

class Scheduler
{
public:
    static Expected<std::unique_ptr<Scheduler>> create(HailoDiffuserSchedulerType scheduler_type);

    virtual hailo_status set_timesteps(size_t num_inference_steps) = 0;
    virtual std::vector<int> get_timesteps() const = 0;
    virtual int get_timestep_by_idx(uint32_t idx) const = 0;
    virtual hailo_status scale_model_input(MemoryView original_sample, MemoryView scaled_sample, int inference_step) = 0;
    virtual float32_t get_init_noise_sigma() const = 0;
    virtual bool should_scale_model_input() const = 0;

    virtual hailo_status step(MemoryView noise_pred, MemoryView latents, int inference_step) = 0;

    virtual ~Scheduler() = default;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_HPP_ */
