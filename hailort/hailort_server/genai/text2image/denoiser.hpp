/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file denoiser.hpp
 * @brief Denoiser manager for running inference and diffusion process
 **/

#ifndef _HAILO_DENOISER_HPP_
#define _HAILO_DENOISER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/genai/text2image/text2image.hpp"
#include "common/utils.hpp"

#include "text2image_server.hpp"

#include "inference_manager.hpp"
#include "utils.hpp"
#include "eigen.hpp"
#include "schedulers/scheduler.hpp"

#include <random>


namespace hailort
{
namespace genai
{

static const uint32_t DEFAULT_GENERATION_SEED = 0;
static const std::string INPUT_LAYER_LATENT_SUFFIX = "input_layer1";
static const std::string INPUT_LAYER_TIMESTEPS_SUFFIX = "input_layer2";
static const std::string INPUT_LAYER_EMBEDDINGS_SUFFIX = "input_layer3";
static const std::string OUTPUT_LAYER_NOISE_PREDICATION_SUFFIX = "conv258";

class Denoiser
{
public:
    static Expected<std::unique_ptr<Denoiser>> create(Hef denoiser_hef, std::shared_ptr<hailort::VDevice> vdevice,
        HailoDiffuserSchedulerType scheduler_type, std::shared_ptr<std::atomic<Text2ImageServer::State>> state);

    hailo_status execute(const Text2ImageGeneratorParams &params, bool has_negative_prompt);
    void set_seed(uint32_t seed);

    hailo_status allocate_all_buffers();
    Expected<MemoryView> get_input_buffer(const std::string &input_suffix);
    Expected<MemoryView> get_output_buffer();
    Expected<MemoryView> get_positive_input_buffer();
    Expected<MemoryView> get_negative_input_buffer();
    MemoryView get_initial_noise_buffer();
    void set_initial_noise_flag(bool is_initial_noise_set);

    hailo_format_t get_input_latent_noise_format() const;
    hailo_3d_image_shape_t get_input_latent_noise_shape() const;

    Denoiser(std::vector<std::unique_ptr<InferenceManager>> &&m_inference_managers_denoisers,
        std::unique_ptr<Scheduler> &&scheduler, hailo_format_t input_latent_noise_format, hailo_3d_image_shape_t input_latent_noise_shape,
        std::shared_ptr<std::atomic<Text2ImageServer::State>> state);
    Denoiser(Denoiser &&) = delete;
    Denoiser(const Denoiser &) = delete;
    Denoiser &operator=(Denoiser &&) = delete;
    Denoiser &operator=(const Denoiser &) = delete;
    virtual ~Denoiser() = default;

private:
    struct TimestepsConfig
    {
        // These params are usually used with the default values, but are configurable in other models.
        // TODO: HRT-16824 - Get this params from hef
        bool flip_sin_to_cos = true;
        float32_t downscale_freq_shift = 0;
        float32_t scale = 1;
        float32_t max_period = 10000.f;
    };

    hailo_status timesteps_preprocessing(uint32_t timestep_idx);
    void fill_random_buffer(MemoryView buffer);
    Expected<BufferPtr> allocate_input_buffer(const std::string &input_suffix);
    Expected<BufferPtr> allocate_output_buffer();
    hailo_status init_timesteps_resources();
    hailo_status swap_prompts_buffers(std::unique_ptr<InferenceManager> &inference_manager, MemoryView curr_input, MemoryView curr_output);
    void cfg(float32_t guidance_scale);

    std::vector<std::unique_ptr<InferenceManager>> m_inference_managers_denoisers;
    std::unique_ptr<Scheduler> m_diffusion_scheduler;
    std::map<std::string, BufferPtr> m_denoiser_buffers;
    hailo_format_t m_input_latent_noise_format;
    hailo_3d_image_shape_t m_input_latent_noise_shape;

    bool m_is_initial_noise_set;
    std::mt19937 m_random_generator;
    TimestepsConfig m_timesteps_config;
    Eigen::RowVectorXf m_exponent;
    BufferPtr m_initial_noise_buffer;

    // Buffers
    BufferPtr m_input_timesteps_buffer;
    BufferPtr m_input_latents_scaled_buffer;
    BufferPtr m_input_latents_original_buffer;


    BufferPtr m_input_positive_embeddings_buffer;
    BufferPtr m_input_negative_embeddings_buffer;

    BufferPtr m_output_positive_buffer;
    BufferPtr m_output_negative_buffer;

    std::shared_ptr<std::atomic<Text2ImageServer::State>> m_state;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_DENOISER_HPP_ */
