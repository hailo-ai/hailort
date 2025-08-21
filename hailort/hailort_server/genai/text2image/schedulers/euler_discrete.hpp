/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file euler_discrete.hpp
 * @brief Euler Discrete Scheduler
 *  https://arxiv.org/pdf/2010.02502.pdf - Denoising Diffusion Implicit Models
 *
 *  Inspired by https://github.com/openvinotoolkit/openvino.genai/blob/f274cb33f4b05332a93230fd3f81aa875410fb1a/src/cpp/src/image_generation/schedulers/euler_discrete.cpp
 *  (Apache License 2.0)
 **/

#ifndef _HAILO_GENAI_EULER_DISCRETE_HPP_
#define _HAILO_GENAI_EULER_DISCRETE_HPP_

#include "hailo/hailort.h"
#include "scheduler.hpp"
#include "eigen.hpp"

namespace hailort
{
namespace genai
{

class EulerDiscreteScheduler : public Scheduler {
public:

    // TODO: HRT-17267 - Consider using the json config in the future
    struct SchedulerConfig
    {
        uint32_t num_train_timesteps = 1000;
        float32_t beta_start = 0.00085f;
        float32_t beta_end = 0.012f;
        BetaSchedule beta_schedule = BetaSchedule::SCALED_LINEAR;
        std::vector<float32_t> trained_betas = {};
        PredictionType prediction_type = PredictionType::EPSILON;
        InterpolationType interpolation_type = InterpolationType::LINEAR;
        bool use_karras_sigmas = false;
        bool use_exponential_sigmas = false;
        bool use_beta_sigmas = false;
        float32_t sigma_min = 0.0f;
        float32_t sigma_max = 0.0f;
        TimestepSpacing timestep_spacing = TimestepSpacing::LINSPACE;
        TimestepType timestep_type = TimestepType::DISCRETE;
        size_t steps_offset = 1;
        bool rescale_betas_zero_snr = false;
    };

    static Expected<std::unique_ptr<EulerDiscreteScheduler>> create();

    virtual hailo_status scale_model_input(MemoryView original_sample, MemoryView scaled_sample, int inference_step) override;
    virtual bool should_scale_model_input() const override;
    virtual hailo_status set_timesteps(size_t num_inference_steps) override;
    virtual std::vector<int> get_timesteps() const override;
    virtual int get_timestep_by_idx(uint32_t idx) const override;
    virtual float32_t get_init_noise_sigma() const override;
    virtual hailo_status step(MemoryView noise_pred, MemoryView latents, int inference_step) override;

    EulerDiscreteScheduler(const SchedulerConfig &&scheduler_config, Eigen::VectorXf &&alphas_cumprod,
        Eigen::VectorXf &&sigmas);
    EulerDiscreteScheduler(EulerDiscreteScheduler &&) = delete;
    EulerDiscreteScheduler(const EulerDiscreteScheduler &) = delete;
    EulerDiscreteScheduler &operator=(EulerDiscreteScheduler &&) = delete;
    EulerDiscreteScheduler &operator=(const EulerDiscreteScheduler &) = delete;
    virtual ~EulerDiscreteScheduler() = default;

private:
    SchedulerConfig m_config;
    Eigen::VectorXf m_alphas_cumprod;
    Eigen::VectorXf m_sigmas;
    std::vector<int> m_timesteps;

    size_t m_steps_count;
    int m_step_index;
    int m_begin_index;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_EULER_DISCRETE_HPP_ */
