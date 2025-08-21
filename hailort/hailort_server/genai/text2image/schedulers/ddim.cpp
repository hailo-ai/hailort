/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file ddim.cpp
 * @brief DDIM Scheduler implementation
 *  https://arxiv.org/pdf/2010.02502.pdf - Denoising Diffusion Implicit Models
 *
 *  Inspired by https://github.com/openvinotoolkit/openvino.genai/blob/f274cb33f4b05332a93230fd3f81aa875410fb1a/src/cpp/src/image_generation/schedulers/ddim.cpp
 *  (Apache License 2.0)
 **/

#include "ddim.hpp"
#include "hailo/hailort.h"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<DDIMScheduler>> DDIMScheduler::create()
{
    SchedulerConfig config; // default config values
    return DDIMScheduler::create(config);
}

Expected<std::unique_ptr<DDIMScheduler>> DDIMScheduler::create(const SchedulerConfig &scheduler_config)
{
    CHECK(!scheduler_config.clip_sample, HAILO_NOT_SUPPORTED, "DDIM Scheduler's config parameter `clip_sample` is not supported");
    CHECK(!scheduler_config.set_alpha_to_one, HAILO_NOT_SUPPORTED, "DDIM Scheduler's config parameter `set_alpha_to_one` is not supported");
    CHECK(!scheduler_config.thresholding, HAILO_NOT_SUPPORTED, "DDIM Scheduler's config parameter `thresholding` is not supported");
    CHECK(!scheduler_config.rescale_betas_zero_snr, HAILO_NOT_SUPPORTED, "DDIM Scheduler's config parameter `rescale_betas_zero_snr` is not supported");
    CHECK(scheduler_config.timestep_spacing == TimestepSpacing::LEADING, HAILO_NOT_SUPPORTED, "DDIM Scheduler's config parameter `timestep_spacing` only supports TimestepSpacing::LEADING");

    Eigen::VectorXf betas;
    if (!scheduler_config.trained_betas.empty()) {
        betas = Eigen::Map<const Eigen::VectorXf>(scheduler_config.trained_betas.data(), scheduler_config.trained_betas.size());
    } else if (scheduler_config.beta_schedule == BetaSchedule::LINEAR) {
        betas = Eigen::VectorXf::LinSpaced(scheduler_config.num_train_timesteps, scheduler_config.beta_start, scheduler_config.beta_end);
    } else if (scheduler_config.beta_schedule == BetaSchedule::SCALED_LINEAR) {
    // Default config flow
        float32_t start = std::sqrt(scheduler_config.beta_start);
        float32_t end = std::sqrt(scheduler_config.beta_end);
        betas = Eigen::VectorXf::LinSpaced(scheduler_config.num_train_timesteps, start, end).array().square();
    } else {
        LOGGER__ERROR("Failed to create DDIMScheduler, got invalid argument `beta_schedule` {}", static_cast<int>(scheduler_config.beta_schedule));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    Eigen::VectorXf alphas_cumprod = Eigen::VectorXf::Ones(betas.size());  
    alphas_cumprod(0) = 1.0f - betas(0);
    for (int i = 1; i < betas.size(); ++i) {  
        alphas_cumprod(i) = alphas_cumprod(i - 1) * (1.0f - betas(i));  
    }
    auto final_alpha_cumprod = scheduler_config.set_alpha_to_one ? 1.0f : alphas_cumprod(0);

    auto ddim_ptr = make_unique_nothrow<DDIMScheduler>(scheduler_config, std::move(alphas_cumprod), final_alpha_cumprod);
    CHECK_NOT_NULL_AS_EXPECTED(ddim_ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return ddim_ptr;
}

DDIMScheduler::DDIMScheduler(const SchedulerConfig &scheduler_config, Eigen::VectorXf &&alphas_cumprod, float32_t final_alpha_cumprod) :
    m_config(scheduler_config),
    m_alphas_cumprod(std::move(alphas_cumprod)),
    m_final_alpha_cumprod(final_alpha_cumprod)
{}

hailo_status DDIMScheduler::set_timesteps(size_t steps_count)
{
    CHECK(steps_count <= m_config.num_train_timesteps, HAILO_INVALID_ARGUMENT, "`steps_count` ({}) cannot be larger than the model's trained timesteps ({})",
        steps_count, m_config.num_train_timesteps);

    m_steps_count = steps_count;
    m_timesteps.clear();
    m_timesteps.reserve(m_steps_count + 1);

    switch (m_config.timestep_spacing) {
        case TimestepSpacing::LEADING:
        {
            int step_ratio = static_cast<int>(m_config.num_train_timesteps / m_steps_count);
            for (int i = static_cast<int>(m_steps_count - 1); i > -2 ; --i) {
                m_timesteps.emplace_back(i * step_ratio + static_cast<int>(m_config.steps_offset));
            }
            break;
        }
        default:
            LOGGER__ERROR("TimestepSpacing {} is not supported", static_cast<int>(m_config.timestep_spacing));
            return HAILO_NOT_SUPPORTED;
    }
    return HAILO_SUCCESS;
}

hailo_status DDIMScheduler::step(MemoryView noise_pred, MemoryView latents, int inference_step)
{
    size_t timestep = m_timesteps[inference_step];
    int prev_timestep = static_cast<int>(timestep - m_config.num_train_timesteps / m_steps_count);

    float32_t alpha_prod_t = m_alphas_cumprod(timestep);
    float32_t alpha_prod_t_prev = (prev_timestep >= 0) ? m_alphas_cumprod(prev_timestep) : m_final_alpha_cumprod;
    float32_t beta_prod_t = 1.0f - alpha_prod_t;

    Eigen::Map<Eigen::VectorXf> latents_vec(reinterpret_cast<float32_t*>(latents.data()), latents.size() / sizeof(float32_t));
    Eigen::Map<Eigen::VectorXf> noise_pred_vec(reinterpret_cast<float32_t*>(noise_pred.data()), noise_pred.size() / sizeof(float32_t));

    // TODO: HRT-16606 - get sizes from denoiser and create these vectors in DDIMScheduler::create
    Eigen::VectorXf pred_original_sample(latents_vec.size());
    Eigen::VectorXf pred_epsilon(noise_pred_vec.size());

    switch (m_config.prediction_type) {
        case PredictionType::EPSILON:
        // Default config flow
            pred_original_sample = (latents_vec - noise_pred_vec * std::sqrt(beta_prod_t)) / std::sqrt(alpha_prod_t);
            pred_epsilon = noise_pred_vec;
            break;

        case PredictionType::SAMPLE:
            pred_original_sample = noise_pred_vec;
            pred_epsilon = (latents_vec - pred_original_sample * std::sqrt(alpha_prod_t)) / std::sqrt(beta_prod_t);
            break;

        case PredictionType::V_PREDICTION:
            pred_original_sample = std::sqrt(alpha_prod_t) * latents_vec - std::sqrt(beta_prod_t) * noise_pred_vec;
            pred_epsilon = std::sqrt(alpha_prod_t) * noise_pred_vec + std::sqrt(beta_prod_t) * latents_vec;
            break;

        default:
            LOGGER__ERROR("Unsupported prediction type {}", static_cast<int>(m_config.prediction_type));
            return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    Eigen::VectorXf pred_sample_direction = pred_epsilon * std::sqrt(1.0f - alpha_prod_t_prev);

    // Filling latents memview as the output
    latents_vec = std::sqrt(alpha_prod_t_prev) * pred_original_sample + pred_sample_direction;

    return HAILO_SUCCESS;
}

std::vector<int> DDIMScheduler::get_timesteps() const
{
    return m_timesteps;
}

int DDIMScheduler::get_timestep_by_idx(uint32_t idx) const
{
    assert(m_timesteps.size() > idx);
    return m_timesteps[idx];
}

float32_t DDIMScheduler::get_init_noise_sigma() const
{
    return 1.0f;
}

bool DDIMScheduler::should_scale_model_input() const
{
    return false;
}

hailo_status DDIMScheduler::scale_model_input(MemoryView original_sample, MemoryView scaled_sample, int inference_step)
{
    // In DDIM scheduler `scale_model_input` does not affect the sample.
    CHECK((original_sample.size() == scaled_sample.size()) && (original_sample.data() == scaled_sample.data()),
        HAILO_INVALID_ARGUMENT, "Failed to 'scale_model_input' of DDIM scheduler. 'original_sample' and 'scaled_sample' are different but expected to be the same");


    (void)inference_step;
    return HAILO_SUCCESS;
}

} // namespace genai
} /* namespace hailort */
