/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file denoiser.cpp
 * @brief Denoiser implementation
 **/

#include "denoiser.hpp"
#include "hailo/hailort.h"
#include "text_encoder.hpp"

namespace hailort
{
namespace genai
{

constexpr auto DENOISER_JOB_WAIT_TIMEOUT = std::chrono::seconds(3);

Expected<std::unique_ptr<Denoiser>> Denoiser::create(Hef denoiser_hef, std::shared_ptr<hailort::VDevice> vdevice, HailoDiffuserSchedulerType scheduler_type,
    std::shared_ptr<std::atomic<Text2ImageServer::State>> state)
{
    // TODO: HRT-16908 - Consider adding a new "element" for multi-ngs inference manager
    auto ngs_names = denoiser_hef.get_network_groups_names();
    std::sort(ngs_names.begin(), ngs_names.end());

    hailo_format_t input_latent_noise_format = {};
    hailo_3d_image_shape_t input_latent_noise_shape = {};

    // Create inference manager per NG
    std::vector<std::unique_ptr<InferenceManager>> inference_managers;
    for (const auto &network_group_name : ngs_names) {
        TRY(auto inference_manager_denoiser, InferenceManager::create(vdevice, denoiser_hef, network_group_name));
        inference_manager_denoiser->get_model()->set_batch_size(INPUT_PROMPTS_BATCH_SIZE);

        // Set all input and output format types to float32.
        for (const auto &input_name : inference_manager_denoiser->get_model()->get_input_names()) {
            TRY(auto input, inference_manager_denoiser->get_model()->input(input_name));
            input.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);

            if (has_suffix(input_name, INPUT_LAYER_LATENT_SUFFIX)) {
                input_latent_noise_format = input.format();
                input_latent_noise_shape = input.shape();
            }
        }
        for (const auto &output_name : inference_manager_denoiser->get_model()->get_output_names()) {
            TRY(auto output, inference_manager_denoiser->get_model()->output(output_name));
            output.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
        }

        CHECK_SUCCESS(inference_manager_denoiser->configure());
        inference_managers.emplace_back(std::move(inference_manager_denoiser));
    }

    TRY(auto scheduler, Scheduler::create(scheduler_type));

    auto denoiser_ptr = make_unique_nothrow<Denoiser>(std::move(inference_managers), std::move(scheduler),
        input_latent_noise_format, input_latent_noise_shape, state);
    CHECK_NOT_NULL_AS_EXPECTED(denoiser_ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return denoiser_ptr;
}

Denoiser::Denoiser(std::vector<std::unique_ptr<InferenceManager>> &&inference_managers_denoisers,
    std::unique_ptr<Scheduler> &&scheduler, hailo_format_t input_latent_noise_format, hailo_3d_image_shape_t input_latent_noise_shape,
    std::shared_ptr<std::atomic<Text2ImageServer::State>> state) :
        m_inference_managers_denoisers(std::move(inference_managers_denoisers)),
        m_diffusion_scheduler(std::move(scheduler)),
        m_input_latent_noise_format(input_latent_noise_format),
        m_input_latent_noise_shape(input_latent_noise_shape),
        m_is_initial_noise_set(false),
        m_random_generator(DEFAULT_GENERATION_SEED),
        m_state(state)
{}

void Denoiser::fill_random_buffer(MemoryView buffer)
{
    float32_t* data = buffer.as_pointer<float32_t>();
    size_t size = buffer.size() / sizeof(float32_t);
    Eigen::Map<Eigen::ArrayXf> mapped_noise(data, size);

    if (m_is_initial_noise_set) {
        memcpy(buffer.data(), m_initial_noise_buffer->data(), buffer.size());
    } else {
        std::normal_distribution<float32_t> dist(0.0f, 1.0f);
        mapped_noise = Eigen::ArrayXf::NullaryExpr(size, [&](){ return dist(m_random_generator); });
    }
    mapped_noise *= m_diffusion_scheduler->get_init_noise_sigma();
}

MemoryView Denoiser::get_initial_noise_buffer()
{
    return MemoryView(m_initial_noise_buffer);
}

void Denoiser::set_initial_noise_flag(bool is_initial_noise_set)
{
    m_is_initial_noise_set = is_initial_noise_set;
}

void Denoiser::set_seed(uint32_t seed)
{
    m_random_generator.seed(seed);
}

void Denoiser::cfg(float32_t guidance_scale)
{
    Eigen::Map<Eigen::VectorXf> pos(reinterpret_cast<float32_t*>(m_output_positive_buffer->data()), m_output_positive_buffer->size() / sizeof(float32_t));
    Eigen::Map<Eigen::VectorXf> neg(reinterpret_cast<float32_t*>(m_output_negative_buffer->data()), m_output_negative_buffer->size() / sizeof(float32_t));
    pos = neg + (guidance_scale * (pos - neg));
}

// TODO: HRT-16908 - Consider renaming `denoise_loop` and using an Element interface with execute() function
hailo_status Denoiser::execute(const Text2ImageGeneratorParams &params, bool has_negative_prompt)
{
    LOGGER__GENAI_STATS_START("[text2image-generation] denoise - set timesteps");
    CHECK_SUCCESS(m_diffusion_scheduler->set_timesteps(params.steps_count()));
    LOGGER__GENAI_STATS_END("[text2image-generation] denoise - set timesteps");

    LOGGER__GENAI_STATS_START("[text2image-generation] denoise - prepare models");
    if (has_negative_prompt) {
        // If negative prompt is not empty, we infer on both the positive and negative prompts
        for (auto &inference_manager : m_inference_managers_denoisers) {
            CHECK_SUCCESS(inference_manager->get_configured_model().set_scheduler_threshold(INPUT_PROMPTS_BATCH_SIZE));
            CHECK_SUCCESS(inference_manager->get_configured_model().set_scheduler_timeout(SCHEDULER_TIMEOUT));
        }
    } else {
        // If negative prompt is empty, we infer only on the positive prompt
        for (auto &inference_manager : m_inference_managers_denoisers) {
            CHECK_SUCCESS(inference_manager->get_configured_model().set_scheduler_threshold(DEFAULT_SCHEDULER_THRESHOLD));
            CHECK_SUCCESS(inference_manager->get_configured_model().set_scheduler_timeout(DEFAULT_SCHEDULER_TIMEOUT));
        }
    }
    LOGGER__GENAI_STATS_END("[text2image-generation] denoise - prepare models");

    assert(params.steps_count() % m_inference_managers_denoisers.size() == 0);

    LOGGER__GENAI_STATS_START("[text2image-generation] denoise - init random buffer");
    auto latents_orig_input = MemoryView(m_input_latents_original_buffer);
    fill_random_buffer(latents_orig_input);
    LOGGER__GENAI_STATS_END("[text2image-generation] denoise - init random buffer");

    LOGGER__GENAI_STATS_START("[text2image-generation] denoise - denoiser loop");
    auto steps_per_model = params.steps_count() / m_inference_managers_denoisers.size();
    auto latents_scaled_input = MemoryView(m_input_latents_scaled_buffer);
    auto noise_pred_output = MemoryView(m_output_positive_buffer);
    for (uint32_t curr_step = 0; curr_step < params.steps_count(); curr_step++) {

        if (Text2ImageServer::State::ABORTING == m_state->load()) {
            LOGGER__INFO("Denoiser: Aborting generation, curr_step: {}", curr_step);
            return HAILO_OPERATION_ABORTED;
        }

        LOGGER__GENAI_STATS_START("[text2image-generation-denoise-step] preprocess timesteps");
        auto inference_manager_idx = curr_step / steps_per_model;
        assert(inference_manager_idx < m_inference_managers_denoisers.size());
        auto &inference_manager = m_inference_managers_denoisers[inference_manager_idx];
        CHECK_SUCCESS(timesteps_preprocessing(curr_step));
        LOGGER__GENAI_STATS_END("[text2image-generation-denoise-step] preprocess timesteps");

        LOGGER__GENAI_STATS_START("[text2image-generation-denoise-step] scale model input");
        CHECK_SUCCESS(m_diffusion_scheduler->scale_model_input(latents_orig_input, latents_scaled_input, curr_step));
        LOGGER__GENAI_STATS_END("[text2image-generation-denoise-step] scale model input");

        LOGGER__GENAI_STATS_START("[text2image-generation-denoise-step] denoise inference");
        TRY(auto async_infer_job, inference_manager->generate_async());
        async_infer_job.detach();

        if (has_negative_prompt) {
            CHECK_SUCCESS(swap_prompts_buffers(inference_manager, MemoryView(m_input_negative_embeddings_buffer), MemoryView(m_output_negative_buffer)));
            TRY(async_infer_job, inference_manager->generate_async());
            CHECK_SUCCESS(swap_prompts_buffers(inference_manager, MemoryView(m_input_positive_embeddings_buffer), MemoryView(m_output_positive_buffer)));
        }
        CHECK_SUCCESS(async_infer_job.wait(DENOISER_JOB_WAIT_TIMEOUT));
        LOGGER__GENAI_STATS_END("[text2image-generation-denoise-step] denoise inference");

        if (has_negative_prompt) {
            LOGGER__GENAI_STATS_START("[text2image-generation-denoise-step] config guidance scale");
            cfg(params.guidance_scale());
            LOGGER__GENAI_STATS_END("[text2image-generation-denoise-step] config guidance scale");
        }

        LOGGER__GENAI_STATS_START("[text2image-generation-denoise-step] diffusion scheduler");
        CHECK_SUCCESS(m_diffusion_scheduler->step(noise_pred_output, latents_orig_input, curr_step));
        LOGGER__GENAI_STATS_END("[text2image-generation-denoise-step] diffusion scheduler");
    }
    LOGGER__GENAI_STATS_END("[text2image-generation] denoise - denoiser loop");

    return HAILO_SUCCESS;
}

// TODO: Consider exporting timesteps to another module
hailo_status Denoiser::init_timesteps_resources()
{
    // Note: Assumes float32 format, as set in the constructor. Update both if changed.
    auto timesteps_embedding_dim = m_input_timesteps_buffer->size() / sizeof(float32_t);
    CHECK((timesteps_embedding_dim % 2 == 0), HAILO_NOT_SUPPORTED,
        "Failed to init timesteps layer buffer, Zero padding is not supported yet. Dimension should be an even number, got {}", timesteps_embedding_dim);

    auto half_dim = timesteps_embedding_dim / 2;
    float32_t half_dim_float = static_cast<float32_t>(half_dim);

    auto lin = Eigen::RowVectorXf::LinSpaced(half_dim, 0, half_dim_float - 1);
    Eigen::RowVectorXf exponent = -std::log(m_timesteps_config.max_period) * lin;
    exponent /= (half_dim_float - m_timesteps_config.downscale_freq_shift);

    m_exponent = exponent.array().exp();
    return HAILO_SUCCESS;
}

hailo_status Denoiser::timesteps_preprocessing(uint32_t timestep_idx)
{
    auto timestep = m_diffusion_scheduler->get_timestep_by_idx(timestep_idx);
    auto emb = m_timesteps_config.scale * (timestep * m_exponent);

    // Concatenate sine and cosine embeddings
    auto sin_emb = emb.array().sin();
    auto cos_emb = emb.array().cos();
    Eigen::Map<Eigen::RowVectorXf> timesteps_embedding(reinterpret_cast<float32_t*>(m_input_timesteps_buffer->data()), (m_input_timesteps_buffer->size() / sizeof(float32_t)));
    if (m_timesteps_config.flip_sin_to_cos) {
        timesteps_embedding << cos_emb, sin_emb;
    } else {
        timesteps_embedding << sin_emb, cos_emb;
    }

    return HAILO_SUCCESS;
}

hailo_status Denoiser::swap_prompts_buffers(std::unique_ptr<InferenceManager> &inference_manager, MemoryView curr_input, MemoryView curr_output)
{
    for (const auto &input_name : inference_manager->get_model()->get_input_names()) {
        if (has_suffix(input_name, INPUT_LAYER_EMBEDDINGS_SUFFIX)) {
            CHECK_SUCCESS(inference_manager->set_input_buffer(curr_input, input_name));
        }
    }
    CHECK_SUCCESS(inference_manager->set_output_buffer(curr_output));

    return HAILO_SUCCESS;
}

hailo_status Denoiser::allocate_all_buffers()
{
    // Init embeddings
    TRY(m_input_positive_embeddings_buffer, allocate_input_buffer(INPUT_LAYER_EMBEDDINGS_SUFFIX));
    TRY(m_input_negative_embeddings_buffer, Buffer::create_shared(m_input_positive_embeddings_buffer->size(), 0, BufferStorageParams::create_dma()));

    // Init timesteps
    TRY(m_input_timesteps_buffer, allocate_input_buffer(INPUT_LAYER_TIMESTEPS_SUFFIX));
    CHECK_SUCCESS(init_timesteps_resources());

    // Init latents
    TRY(m_input_latents_scaled_buffer, allocate_input_buffer(INPUT_LAYER_LATENT_SUFFIX));
    if (m_diffusion_scheduler->should_scale_model_input()) {
        TRY(m_input_latents_original_buffer, Buffer::create_shared(m_input_latents_scaled_buffer->size(), BufferStorageParams::create_dma()));
    } else {
        m_input_latents_original_buffer = m_input_latents_scaled_buffer;
    }

    // Init initial noise buffer
    TRY(m_initial_noise_buffer, Buffer::create_shared(m_input_latents_original_buffer->size(), BufferStorageParams::create_dma()));

    // Init output
    TRY(m_output_positive_buffer, allocate_output_buffer());
    TRY(m_output_negative_buffer, Buffer::create_shared(m_output_positive_buffer->size(), BufferStorageParams::create_dma()));
    
    return HAILO_SUCCESS;
}

Expected<BufferPtr> Denoiser::allocate_input_buffer(const std::string &input_suffix)
{
    // Create the buffer and set it to all the NGs.
    BufferPtr buffer = nullptr;
    for (auto &inference_manager : m_inference_managers_denoisers) {
        for (const auto &input_name : inference_manager->get_model()->get_input_names()) {
            if (has_suffix(input_name, input_suffix)) {
                TRY(auto input, inference_manager->get_model()->input(input_name));
                if (buffer == nullptr) {
                    TRY(buffer, Buffer::create_shared(input.get_frame_size(), BufferStorageParams::create_dma()));
                    m_denoiser_buffers[input_suffix] = buffer;
                }
                CHECK_SUCCESS(inference_manager->set_input_buffer(MemoryView(buffer), input_name));
            }
        }
    }
    CHECK((buffer != nullptr), HAILO_INVALID_ARGUMENT, "Allocating input buffer failed, could not found {}", input_suffix);

    return buffer;
}

Expected<BufferPtr> Denoiser::allocate_output_buffer()
{
    // Create the buffer and set it to all the NGs.
    BufferPtr buffer = nullptr;
    for (auto &inference_manager : m_inference_managers_denoisers) {
        TRY(auto output, inference_manager->get_model()->output());
        if (buffer == nullptr) {
            TRY(buffer, Buffer::create_shared(output.get_frame_size(), BufferStorageParams::create_dma()));
            m_denoiser_buffers[OUTPUT_LAYER_NOISE_PREDICATION_SUFFIX] = buffer;
        }
        CHECK_SUCCESS(inference_manager->set_output_buffer(MemoryView(buffer), output.name()));
    }
    assert(buffer != nullptr);
    return buffer;
}

Expected<MemoryView> Denoiser::get_input_buffer(const std::string &input_suffix)
{
    CHECK(contains(m_denoiser_buffers, input_suffix), HAILO_INVALID_ARGUMENT,
        "Get input buffer of denoiser failed. Could not find input with suffix {}, or buffer was not allocated.", input_suffix);
    return MemoryView(m_denoiser_buffers[input_suffix]);
}

Expected<MemoryView> Denoiser::get_positive_input_buffer()
{
    return MemoryView(m_input_positive_embeddings_buffer);
}

Expected<MemoryView> Denoiser::get_negative_input_buffer()
{
    return MemoryView(m_input_negative_embeddings_buffer);
}

Expected<MemoryView> Denoiser::get_output_buffer()
{
    CHECK(contains(m_denoiser_buffers, OUTPUT_LAYER_NOISE_PREDICATION_SUFFIX), HAILO_INVALID_ARGUMENT,
        "Get output buffer of denoiser failed. Output buffer was not allocated");
    return MemoryView(m_denoiser_buffers[OUTPUT_LAYER_NOISE_PREDICATION_SUFFIX]);
}

hailo_format_t Denoiser::get_input_latent_noise_format() const
{
    return m_input_latent_noise_format;
}

hailo_3d_image_shape_t Denoiser::get_input_latent_noise_shape() const
{
    return m_input_latent_noise_shape;
}

} /* namespace genai */
} /* namespace hailort */
