/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file ddim.hpp
 * @brief DDIM Scheduler for the diffusion process.
 *  https://arxiv.org/pdf/2010.02502.pdf - Denoising Diffusion Implicit Models
 *
 *  Inspired by https://github.com/openvinotoolkit/openvino.genai/blob/f274cb33f4b05332a93230fd3f81aa875410fb1a/src/cpp/src/image_generation/schedulers/ddim.hpp
 *  (Apache License 2.0)
 **/

#ifndef _HAILO_DDIM_HPP_
#define _HAILO_DDIM_HPP_

#include "hailo/hailort.h"
#include "scheduler.hpp"
#include "eigen.hpp"


namespace hailort
{
namespace genai
{

class DDIMScheduler : public Scheduler {
public:

    // TODO: Consider using the json config in the future
    struct SchedulerConfig
    {
        uint32_t num_train_timesteps = 1000;
        float32_t beta_start = 0.00085f;
        float32_t beta_end = 0.012f;
        BetaSchedule beta_schedule = BetaSchedule::SCALED_LINEAR;
        std::vector<float32_t> trained_betas = {};
        bool clip_sample = false;
        bool set_alpha_to_one = false;
        size_t steps_offset = 1;
        PredictionType prediction_type = PredictionType::EPSILON;
        bool thresholding = false;
        float32_t dynamic_thresholding_ratio = 0.995f;
        float32_t clip_sample_range = 1.0f;
        float32_t sample_max_value = 1.0f;
        TimestepSpacing timestep_spacing = TimestepSpacing::LEADING;
        bool rescale_betas_zero_snr = false;
    };

    static Expected<std::unique_ptr<DDIMScheduler>> create(const SchedulerConfig &scheduler_config);
    static Expected<std::unique_ptr<DDIMScheduler>> create();

    virtual hailo_status scale_model_input(MemoryView original_sample, MemoryView scaled_sample, int inference_step) override;
    virtual bool should_scale_model_input() const override;
    virtual hailo_status set_timesteps(size_t num_inference_steps) override;
    virtual std::vector<int> get_timesteps() const override;
    virtual int get_timestep_by_idx(uint32_t idx) const override;

    virtual float32_t get_init_noise_sigma() const override;

    virtual hailo_status step(MemoryView noise_pred, MemoryView latents, int inference_step) override;

    DDIMScheduler(const SchedulerConfig &scheduler_config, Eigen::VectorXf &&alphas_cumprod, float32_t final_alpha_cumprod);
    DDIMScheduler(DDIMScheduler &&) = delete;
    DDIMScheduler(const DDIMScheduler &) = delete;
    DDIMScheduler &operator=(DDIMScheduler &&) = delete;
    DDIMScheduler &operator=(const DDIMScheduler &) = delete;
    virtual ~DDIMScheduler() = default;

private:
    SchedulerConfig m_config;
    Eigen::VectorXf m_alphas_cumprod;
    float32_t m_final_alpha_cumprod;

    size_t m_steps_count;
    std::vector<int> m_timesteps;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_DDIM_HPP_ */
