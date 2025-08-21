/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file euler_discrete.cpp
 * @brief Euler Discrete Scheduler implementation
 *  https://arxiv.org/pdf/2010.02502.pdf - Denoising Diffusion Implicit Models
 *
 *  Inspired by https://github.com/openvinotoolkit/openvino.genai/blob/f274cb33f4b05332a93230fd3f81aa875410fb1a/src/cpp/src/image_generation/schedulers/euler_discrete.cpp
 *  (Apache License 2.0)
 **/

#include "euler_discrete.hpp"
#include "hailo/hailort.h"
#include "utils.hpp"

namespace hailort
{
namespace genai
{

#define START_INDEX (-1)

Expected<std::unique_ptr<EulerDiscreteScheduler>> EulerDiscreteScheduler::create()
{
    const SchedulerConfig scheduler_config; // default config

    CHECK(!scheduler_config.use_karras_sigmas, HAILO_NOT_SUPPORTED, "Euler Discrete Scheduler's config parameter `use_karras_sigmas` is not supported");
    CHECK(!scheduler_config.use_exponential_sigmas, HAILO_NOT_SUPPORTED, "Euler Discrete Scheduler's config parameter `use_exponential_sigmas` is not supported");
    CHECK(!scheduler_config.use_beta_sigmas, HAILO_NOT_SUPPORTED, "Euler Discrete Scheduler's config parameter `use_beta_sigmas` is not supported");
    CHECK(!scheduler_config.rescale_betas_zero_snr, HAILO_NOT_SUPPORTED, "Euler Discrete Scheduler's config parameter `rescale_betas_zero_snr` is not supported");
    CHECK(scheduler_config.timestep_spacing == TimestepSpacing::LINSPACE, HAILO_NOT_SUPPORTED, "Euler Discrete Scheduler's config parameter `timestep_spacing` only supports TimestepSpacing::LINSPACE");
    CHECK(scheduler_config.timestep_type == TimestepType::DISCRETE, HAILO_NOT_SUPPORTED, "Euler Discrete Scheduler's config parameter `timestep_type` only supports TimestepType::DISCRETE");

    Eigen::VectorXf betas;
    if (!scheduler_config.trained_betas.empty()) {
        betas = Eigen::Map<const Eigen::VectorXf>(scheduler_config.trained_betas.data(), scheduler_config.trained_betas.size());
    } else if (scheduler_config.beta_schedule == BetaSchedule::LINEAR) {
        betas = Eigen::VectorXf::LinSpaced(scheduler_config.num_train_timesteps, scheduler_config.beta_start, scheduler_config.beta_end);
    } else if (scheduler_config.beta_schedule == BetaSchedule::SCALED_LINEAR) {
        float32_t start = std::sqrt(scheduler_config.beta_start);
        float32_t end = std::sqrt(scheduler_config.beta_end);
        betas = Eigen::VectorXf::LinSpaced(scheduler_config.num_train_timesteps, start, end).array().square();
    } else {
        LOGGER__ERROR("Failed to create EulerDiscreteScheduler, got invalid argument `beta_schedule` {}", static_cast<int>(scheduler_config.beta_schedule));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    Eigen::VectorXf alphas_cumprod = Eigen::VectorXf::Ones(betas.size());  
    alphas_cumprod(0) = 1.0f - betas(0);
    for (int i = 1; i < betas.size(); ++i) {  
        alphas_cumprod(i) = alphas_cumprod(i - 1) * (1.0f - betas(i));  
    }

    Eigen::VectorXf sigmas = Eigen::VectorXf::Zero(alphas_cumprod.size() + 1);  
    for (int i = 0; i < alphas_cumprod.size(); ++i) {
        float32_t alpha = alphas_cumprod(alphas_cumprod.size() - i - 1);
        sigmas(i) = std::sqrt((1.0f - alpha) / alpha);
    }

    auto euler_ptr = make_unique_nothrow<EulerDiscreteScheduler>(std::move(scheduler_config), std::move(alphas_cumprod),
        std::move(sigmas));
    CHECK_NOT_NULL_AS_EXPECTED(euler_ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return euler_ptr;
}

EulerDiscreteScheduler::EulerDiscreteScheduler(const SchedulerConfig &&scheduler_config, Eigen::VectorXf &&alphas_cumprod,
    Eigen::VectorXf &&sigmas) :
    m_config(std::move(scheduler_config)),
    m_alphas_cumprod(std::move(alphas_cumprod)),
    m_sigmas(std::move(sigmas)),
    m_step_index(START_INDEX),
    m_begin_index(START_INDEX)
{}

hailo_status EulerDiscreteScheduler::set_timesteps(size_t steps_count)
{
    CHECK(steps_count <= m_config.num_train_timesteps, HAILO_INVALID_ARGUMENT, "`steps_count` ({}) cannot be larger than the model's trained timesteps ({})",
        steps_count, m_config.num_train_timesteps);
    
    m_steps_count = steps_count;
    m_timesteps.clear();
    m_timesteps.reserve(m_steps_count + 1);
    m_step_index = START_INDEX;
    m_begin_index = START_INDEX;

    // Set timesteps
    switch (m_config.timestep_spacing) {
        case TimestepSpacing::LINSPACE: {
            auto linspaced = Eigen::VectorXf::LinSpaced(m_steps_count, 0.0f,
                static_cast<float32_t>(m_config.num_train_timesteps - 1)).array();
            for (int i = static_cast<int>(linspaced.size() - 1); i >= 0; i--) {
                m_timesteps.push_back(static_cast<int>(std::round(linspaced(i))));
            }
            break;
        }
        default:
            LOGGER__ERROR("Timestep spacing {} is not supported", static_cast<int>(m_config.timestep_spacing));
            return HAILO_NOT_SUPPORTED;
    }

    // Set sigmas
    switch (m_config.interpolation_type) {
        case InterpolationType::LINEAR: {
            Eigen::VectorXf sigmas = ((1.0f - m_alphas_cumprod.array()) / m_alphas_cumprod.array()).sqrt();
            Eigen::VectorXf x_data = Eigen::VectorXf::LinSpaced(sigmas.size(), 0, static_cast<float32_t>(sigmas.size() - 1));
            Eigen::VectorXf t = Eigen::Map<const Eigen::VectorXi>(m_timesteps.data(), m_timesteps.size()).cast<float32_t>();
            m_sigmas = interp(t, x_data, sigmas);
            m_sigmas.conservativeResize(m_sigmas.size() + 1);
            m_sigmas(m_sigmas.size() - 1) = 0;
            break;
        }
        default:
            LOGGER__ERROR("Interpolation type {} is not supported", static_cast<int>(m_config.interpolation_type));
            return HAILO_NOT_SUPPORTED;
    }

    return HAILO_SUCCESS;
}

bool EulerDiscreteScheduler::should_scale_model_input() const
{
    return true;
}

hailo_status EulerDiscreteScheduler::scale_model_input(MemoryView original_sample, MemoryView scaled_sample, int inference_step)
{
    if (START_INDEX == m_step_index) {
        m_step_index = (m_begin_index == START_INDEX) ? inference_step : m_begin_index;
    }

    Eigen::Map<Eigen::VectorXf> original_sample_vec(reinterpret_cast<float32_t*>(original_sample.data()), original_sample.size() / sizeof(float32_t));
    Eigen::Map<Eigen::VectorXf> scaled_sample_vec(reinterpret_cast<float32_t*>(scaled_sample.data()), scaled_sample.size() / sizeof(float32_t));
    assert(m_step_index < m_sigmas.size());
    float32_t sigma = m_sigmas(m_step_index);
    float32_t scale = std::sqrt(sigma * sigma + 1.0f);
    scaled_sample_vec = original_sample_vec / scale;

    return HAILO_SUCCESS;
}

std::vector<int> EulerDiscreteScheduler::get_timesteps() const
{
    return m_timesteps;
}

int EulerDiscreteScheduler::get_timestep_by_idx(uint32_t idx) const
{
    assert(idx < m_timesteps.size());
    return m_timesteps[idx];
}

float32_t EulerDiscreteScheduler::get_init_noise_sigma() const
{
    return m_sigmas.maxCoeff();
}

hailo_status EulerDiscreteScheduler::step(MemoryView noise_pred, MemoryView latents, int inference_step)
{
    // Note: In Euler Discrete method, the timesteps are not being used in the step function
    // The method uses the sigmas in the text2image diffusion.
    (void)inference_step;

    assert(m_step_index < m_sigmas.size());
    auto sigma = m_sigmas[m_step_index];
    Eigen::Map<Eigen::VectorXf> latents_vec(reinterpret_cast<float32_t*>(latents.data()), latents.size() / sizeof(float32_t));
    Eigen::Map<Eigen::VectorXf> noise_pred_vec(reinterpret_cast<float32_t*>(noise_pred.data()), noise_pred.size() / sizeof(float32_t));

    // TODO: HRT-17267 - Support non-staic sigma based on s_churn, s_tmin, s_tmax
    float32_t gamma = 0.0f;
    float sigma_hat = sigma * (gamma + 1);

    Eigen::VectorXf pred_original_sample(latents_vec.size());
    switch (m_config.prediction_type) {
        case PredictionType::EPSILON:
            pred_original_sample = latents_vec - (sigma_hat * noise_pred_vec);
            break;
        default:
            LOGGER__ERROR("Unsupported prediction type {}", static_cast<int>(m_config.prediction_type));
            return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    float32_t dt = m_sigmas[m_step_index + 1] - sigma_hat;
    
    auto derivative = ((latents_vec - pred_original_sample) / sigma_hat);
    latents_vec += (derivative * dt);

    m_step_index += 1;

    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
