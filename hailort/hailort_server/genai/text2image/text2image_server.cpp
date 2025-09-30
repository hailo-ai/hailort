/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text2image_server.cpp
 * @brief Implementation for Text2Image server
 **/

#include "text2image_server.hpp"
#include "hailo/hailort.h"
#include "hailo/quantization.hpp"
#include "hailo/hailort_defaults.hpp"
#include "hailo/genai/common.hpp"
#include "common/file_utils.hpp"
#include "utils.hpp"
#include "denoiser.hpp"

namespace hailort
{
namespace genai
{

static const std::string ACK = "<success>";

Expected<std::string> get_path_from_model_type(Text2ImageModelType model_type)
{
    switch (model_type)
    {
    case Text2ImageModelType::DENOISE:
        return std::string("/data/models/unet_full_v3_w_extension.hef");
    case Text2ImageModelType::TEXT_ENCODER:
        return std::string("/data/models/clip_text_encoder_vit_large.hef");
    case Text2ImageModelType::IMAGE_DECODER:
        return std::string("/data/models/ip_adapter_faces_sd1_5_decoder.hef");
    case Text2ImageModelType::IP_ADAPTER:
        return std::string("/data/models/buffalo_l_ip-adapter.hef");
    case Text2ImageModelType::SUPER_RESOLUTION:
        return std::string("/data/models/real_esrgan_x2.hef");
    default:
        LOGGER__ERROR("Got invalid model type");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

Expected<std::shared_ptr<Buffer>> Text2ImageServer::get_builitin_hef_buffer(Text2ImageModelType model)
{
    TRY(const auto path, get_path_from_model_type(model));
    TRY(auto buff, read_binary_file(path, BufferStorageParams::create_dma()));
    std::shared_ptr<Buffer> hef_buffer_ptr = make_shared_nothrow<Buffer>(std::move(buff));
    CHECK_NOT_NULL_AS_EXPECTED(hef_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return hef_buffer_ptr;
}

Expected<Hef> Text2ImageServer::get_hef(Text2ImageModelType model, bool is_builtin)
{
    std::shared_ptr<Buffer> hef_buffer;
    if (is_builtin) {
        TRY(hef_buffer, get_builitin_hef_buffer(model));
    }
    else {
        TRY(hef_buffer, m_session.read());
    }
    TRY(auto hef, Hef::create(hef_buffer));
    hef.set_memory_footprint_optimization(true); // zero-copy configuration if possible

    return hef;
}

hailo_status Text2ImageServer::handle_ip_adapter_frame()
{
    TRY(auto ip_adapter_input_memview, m_ip_adapter->get_input_memview());
    TRY(auto bytes_read, m_generation_session.read(ip_adapter_input_memview));
    LOGGER__INFO("Got ip adapter frame");

    auto expected_size = ip_adapter_input_memview.size() / sizeof(float32_t);
    CHECK(bytes_read == expected_size, HAILO_INVALID_ARGUMENT,
        "Mismatch of IP Adapter frame size. expected {}, got {}", expected_size, bytes_read);

    // TODO: HRT-16966 - Remove dequantization after module is adjusted
    hailo_quant_info_t quant_info;
    quant_info.qp_zp = 127.5f;
    quant_info.qp_scale = 1.f / 127.5f;
    auto buffer_elements_count = static_cast<uint32_t>(ip_adapter_input_memview.size() / sizeof(float32_t));
    Quantization::dequantize_output_buffer_in_place<float32_t, uint8_t>(
        reinterpret_cast<float32_t*>(ip_adapter_input_memview.data()), buffer_elements_count, quant_info);

    CHECK_SUCCESS(m_ip_adapter->execute());
    return HAILO_SUCCESS;
}

hailo_status Text2ImageServer::connect_pipeline_buffers()
{
    // Denoiser buffers
    CHECK_SUCCESS(m_denoiser->allocate_all_buffers());
    
    // Note: Embeddings inputs is used as the output buffer of text encoder + ip adapter
    TRY(auto denoiser_input_embeddings_positive, m_denoiser->get_positive_input_buffer());
    TRY(auto denoiser_input_embeddings_negative, m_denoiser->get_negative_input_buffer());

    // Text encoder buffers
    CHECK_SUCCESS(m_text_encoder->allocate_inputs_buffers());

    TRY(auto text_encoder_output_size, m_text_encoder->get_output_frame_size());
    auto text_encoder_output_positive_memview = MemoryView(denoiser_input_embeddings_positive.data(), text_encoder_output_size);
    auto text_encoder_output_negative_memview = MemoryView(denoiser_input_embeddings_negative.data(), text_encoder_output_size);
    m_text_encoder->set_output_pos_buffer(text_encoder_output_positive_memview);
    m_text_encoder->set_output_neg_buffer(text_encoder_output_negative_memview);

    // Ip Adapter buffers
    if (m_ip_adapter) {
        TRY(auto ip_adapter_output_size, m_ip_adapter->get_output_frame_size());
        CHECK(text_encoder_output_size + ip_adapter_output_size == denoiser_input_embeddings_positive.size(), HAILO_INVALID_ARGUMENT,
            "Mismatch of ip adapter and denoiser hefs. Text encoder output size + ip adapter output size must be equal to the denoiser input embeddings size");

        TRY(auto ip_adapter_input, m_ip_adapter->allocate_input_buffer());
        (void)ip_adapter_input;

        auto ip_adapter_output_memview = MemoryView((denoiser_input_embeddings_positive.data() + text_encoder_output_size), ip_adapter_output_size);
        CHECK_SUCCESS(m_ip_adapter->set_output_buffer(ip_adapter_output_memview));
    }

    // Image decoder
    // Note: The denoiser latents input is also the output of the scheduler's post-process
    TRY(auto denoiser_latents_input, m_denoiser->get_input_buffer(INPUT_LAYER_LATENT_SUFFIX));
    CHECK_SUCCESS(m_image_decoder->set_input_buffer(denoiser_latents_input)); 
    TRY(auto image_decoder_output, m_image_decoder->allocate_output_buffer());

    // Super resulotion
    if (m_has_super_resolution) {
        CHECK_SUCCESS(m_super_resolution->set_input_buffer(image_decoder_output));
        TRY(auto sr_output, m_super_resolution->allocate_output_buffer());

        // TODO: HRT-17086 - Remove
        auto final_output_result_size = sr_output.size() / sizeof(float32_t);
        TRY(m_output_result, Buffer::create_shared(final_output_result_size, BufferStorageParams::create_dma()));
    } else {
        // TODO: HRT-17086 - Remove
        auto final_output_result_size = image_decoder_output.size() / sizeof(float32_t);
        TRY(m_output_result, Buffer::create_shared(final_output_result_size, BufferStorageParams::create_dma()));
    }

    return HAILO_SUCCESS;
}

hailo_status Text2ImageServer::init_denoiser(std::shared_ptr<hailort::VDevice> vdevice, HailoDiffuserSchedulerType scheduler_type, bool is_builtin)
{
    LOGGER__GENAI_STATS_START("[create-text2image] get denoiser HEF");
    TRY(auto denoise_hef, get_hef(Text2ImageModelType::DENOISE, is_builtin));
    LOGGER__GENAI_STATS_END("[create-text2image] get denoiser HEF");

    LOGGER__GENAI_STATS_START("[create-text2image] create denoiser element");
    TRY(m_denoiser, Denoiser::create(denoise_hef, vdevice, scheduler_type, m_state));
    LOGGER__GENAI_STATS_END("[create-text2image] create denoiser element");
    return HAILO_SUCCESS;
}

hailo_status Text2ImageServer::init_text_encoder(std::shared_ptr<hailort::VDevice> vdevice, bool is_builtin)
{
    LOGGER__GENAI_STATS_START("[create-text2image] get text encoder HEF");
    TRY(auto text_encoder_hef, get_hef(Text2ImageModelType::TEXT_ENCODER, is_builtin));
    LOGGER__GENAI_STATS_END("[create-text2image] get text encoder HEF");

    LOGGER__GENAI_STATS_START("[create-text2image] create text encoder element");
    TRY(m_text_encoder, TextEncoder::create(text_encoder_hef, vdevice));
    LOGGER__GENAI_STATS_END("[create-text2image] create text encoder element");
    return HAILO_SUCCESS;
}

hailo_status Text2ImageServer::init_image_decoder(std::shared_ptr<hailort::VDevice> vdevice, bool is_builtin)
{
    LOGGER__GENAI_STATS_START("[create-text2image] get image decoder HEF");
    TRY(auto image_decoder_hef, get_hef(Text2ImageModelType::IMAGE_DECODER, is_builtin)); 
    LOGGER__GENAI_STATS_END("[create-text2image] get image decoder HEF");

    LOGGER__GENAI_STATS_START("[create-text2image] create image decoder element");
    TRY(m_image_decoder, SingleIOModel::create(image_decoder_hef, vdevice, HAILO_FORMAT_TYPE_FLOAT32, HAILO_FORMAT_TYPE_FLOAT32));
    LOGGER__GENAI_STATS_END("[create-text2image] create image decoder element");
    return HAILO_SUCCESS;
}

hailo_status Text2ImageServer::init_ip_adapter(std::shared_ptr<hailort::VDevice> vdevice, bool is_builtin)
{
    LOGGER__GENAI_STATS_START("[create-text2image] get ip adapter HEF");
    TRY(auto ip_adapter_hef, get_hef(Text2ImageModelType::IP_ADAPTER, is_builtin));
    LOGGER__GENAI_STATS_END("[create-text2image] get ip adapter HEF");

    LOGGER__GENAI_STATS_START("[create-text2image] create ip adapter element");
    TRY(m_ip_adapter, SingleIOModel::create(ip_adapter_hef, vdevice, HAILO_FORMAT_TYPE_FLOAT32, HAILO_FORMAT_TYPE_FLOAT32));
    LOGGER__GENAI_STATS_END("[create-text2image] create ip adapter element");

    return HAILO_SUCCESS;
}

hailo_status Text2ImageServer::init_super_resolution(std::shared_ptr<hailort::VDevice> vdevice)
{
    LOGGER__GENAI_STATS_START("[create-text2image] get super resolution HEF");
    TRY(auto hef_buffer_ptr, get_builitin_hef_buffer(Text2ImageModelType::SUPER_RESOLUTION));
    TRY(auto super_resolution_hef, Hef::create(hef_buffer_ptr));
    LOGGER__GENAI_STATS_END("[create-text2image] get super resolution HEF");
    // Need to add this mem-optimization if super-resolution model is used. requires new HEF
    // super_resolution_hef.set_memory_footprint_optimization(true); // zero-copy configuration if possible
    LOGGER__GENAI_STATS_START("[create-text2image] create super resolution element");
    TRY(m_super_resolution, SingleIOModel::create(super_resolution_hef, vdevice, HAILO_FORMAT_TYPE_FLOAT32, HAILO_FORMAT_TYPE_FLOAT32));
    LOGGER__GENAI_STATS_END("[create-text2image] create super resolution element");
    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageServer::handle_create_text2image_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create-text2image] create vdevice");
    TRY_AS_HRPC_STATUS(auto create_info, Text2ImageCreateSerializer::deserialize_request(request), Text2ImageCreateSerializer);
    m_has_super_resolution = create_info.is_builtin;
    auto &has_ip_adapter = create_info.is_ip_adapter;
    auto &scheduler_type = create_info.scheduler_type;
    auto &group_id = create_info.group_id;

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, hailort::VDevice::create_shared(params), Text2ImageCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-text2image] create vdevice");

    CHECK_SUCCESS_AS_HRPC_STATUS(init_text_encoder(vdevice, create_info.is_builtin), Text2ImageCreateSerializer);
    CHECK_SUCCESS_AS_HRPC_STATUS(init_denoiser(vdevice, scheduler_type, create_info.is_builtin), Text2ImageCreateSerializer);
    CHECK_SUCCESS_AS_HRPC_STATUS(init_image_decoder(vdevice, create_info.is_builtin), Text2ImageCreateSerializer);

    if (has_ip_adapter) {
        CHECK_SUCCESS_AS_HRPC_STATUS(init_ip_adapter(vdevice, create_info.is_builtin), Text2ImageCreateSerializer);
    }

    TRY_AS_HRPC_STATUS(auto output, m_image_decoder->get_model()->output(), Text2ImageCreateSerializer);
    if (m_has_super_resolution) {
        CHECK_SUCCESS_AS_HRPC_STATUS(init_super_resolution(vdevice), Text2ImageCreateSerializer);

        TRY_AS_HRPC_STATUS(output, m_super_resolution->get_model()->output(), Text2ImageCreateSerializer);
    }

    LOGGER__GENAI_STATS_START("[create-text2image] connect pipeline buffers");
    CHECK_SUCCESS_AS_HRPC_STATUS(connect_pipeline_buffers(), Text2ImageCreateSerializer);
    LOGGER__GENAI_STATS_END("[create-text2image] connect pipeline buffers");

    // TODO: HRT-16824 - Get from hef
    m_generator_params = hailort::genai::Text2ImageGeneratorParams();
    m_generator_params.m_samples_count = TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE;
    m_generator_params.m_steps_count = TEXT2IMAGE_STEPS_COUNT_DEFAULT_VALUE;
    m_generator_params.m_guidance_scale = TEXT2IMAGE_GUIDANCE_SCALE_DEFAULT_VALUE;
    m_generator_params.m_seed = HAILO_RANDOM_SEED;

    hailo_3d_image_shape_t output_shape = output.shape();
    hailo_format_t output_format = output.format();
    output_format.type = HAILO_FORMAT_TYPE_UINT8; // TODO: HRT-17086 - Remove once model's output type is as the actual output type

    auto input_latent_noise_format = m_denoiser->get_input_latent_noise_format();
    auto input_latent_noise_shape = m_denoiser->get_input_latent_noise_shape();

    TRY_AS_HRPC_STATUS(auto reply, Text2ImageCreateSerializer::serialize_reply(HAILO_SUCCESS, output_shape, output_format,
        input_latent_noise_shape, input_latent_noise_format), Text2ImageCreateSerializer);
    return reply;
}

Expected<Buffer> Text2ImageServer::handle_create_generator_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(m_generator_params, Text2ImageGeneratorCreateSerializer::deserialize_request(request), Text2ImageGeneratorCreateSerializer);
    CHECK_AS_HRPC_STATUS(State::READY == m_state->load(), HAILO_INVALID_OPERATION, Text2ImageGeneratorSetInitialNoiseSerializer);

    if (m_generator_params.m_seed == HAILO_RANDOM_SEED) {
        m_generator_params.m_seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    m_denoiser->set_seed(m_generator_params.m_seed);
    m_denoiser->set_initial_noise_flag(false);

    TRY_AS_HRPC_STATUS(auto reply, Text2ImageGeneratorCreateSerializer::serialize_reply(HAILO_SUCCESS), Text2ImageGeneratorCreateSerializer);
    return reply;
}

Expected<Buffer> Text2ImageServer::handle_get_ip_adapter_frame_info_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(Text2ImageGetIPAdapterFrameInfoSerializer::deserialize_request(request), Text2ImageGetIPAdapterFrameInfoSerializer);

    CHECK_AS_HRPC_STATUS(nullptr != m_ip_adapter, HAILO_INVALID_OPERATION, Text2ImageGetIPAdapterFrameInfoSerializer);
    TRY_AS_HRPC_STATUS(auto ip_adapter_input, m_ip_adapter->get_model()->input(), Text2ImageGetIPAdapterFrameInfoSerializer);
    auto input_frame_shape = ip_adapter_input.shape();
    auto input_frame_format = ip_adapter_input.format();
    input_frame_format.type = HAILO_FORMAT_TYPE_UINT8; // HRT-16966 - Remove
 
    TRY_AS_HRPC_STATUS(auto reply, Text2ImageGetIPAdapterFrameInfoSerializer::serialize_reply(HAILO_SUCCESS, input_frame_shape, input_frame_format), Text2ImageGetIPAdapterFrameInfoSerializer);
    return reply;
}

// TODO: HRT-17086 - Remove
void pre_super_resolution_process(MemoryView buffer)
{
    float32_t* data = reinterpret_cast<float32_t*>(buffer.data());
    const size_t elements_count = buffer.size() / sizeof(float32_t);
    
    for (size_t i = 0; i < elements_count; ++i) {
        // Convert [-1,1] range to [0,255] range in one operation with clamping
        data[i] = clamp((data[i] / 2.0f + 0.5f), 0.0f, 1.0f) * 255.0f;
    }
}

// TODO: HRT-17086 - Remove
void post_super_resolution_process(MemoryView input_buffer, MemoryView output_buffer)
{
    const float32_t* input_ptr = reinterpret_cast<const float32_t*>(input_buffer.data());
    uint8_t* output_ptr = reinterpret_cast<uint8_t*>(output_buffer.data());
    const size_t element_count = output_buffer.size();
    
    for (size_t i = 0; i < element_count; ++i) {
        // Clamp to [0,1] range and scale to [0,255] in one operation
        output_ptr[i] = static_cast<uint8_t>(clamp(input_ptr[i], 0.0f, 1.0f) * 255.0f);
    }
}

// TODO: HRT-17086 - Remove
void post_process_output_decoder(MemoryView decoder_output, MemoryView final_buffer)
{
    const float32_t* input_ptr = reinterpret_cast<const float32_t*>(decoder_output.data());
    uint8_t* output_ptr = reinterpret_cast<uint8_t*>(final_buffer.data());
    const size_t element_count = decoder_output.size() / sizeof(float32_t);
    
    for (size_t i = 0; i < element_count; ++i) {
        // Convert from [-1,1] range to [0,255] range
        // 1. Normalize to [0,1]: (x/2 + 0.5)
        // 2. Clamp to [0,1]
        // 3. Scale to [0,255]
        float32_t normalized = clamp((input_ptr[i] / 2.0f + 0.5f), 0.0f, 1.0f);
        output_ptr[i] = static_cast<uint8_t>(normalized * 255.0f);
    }
}

hailo_status Text2ImageServer::check_abort_state()
{
    if (State::ABORTING == m_state->load()) {
        LOGGER__INFO("Aborting generation");
        return HAILO_OPERATION_ABORTED;
    }
    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageServer::handle_generate_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto has_negative_prompt, Text2ImageGeneratorGenerateSerializer::deserialize_request(request), Text2ImageGeneratorGenerateSerializer);

    TRY_V_AS_HRPC_STATUS(auto positive_prompt_buffer, m_generation_session.read(), Text2ImageGeneratorGenerateSerializer);
    auto positive_prompt = positive_prompt_buffer->to_string();
    LOGGER__INFO("Positive prompt - {}", positive_prompt);

    std::string negative_prompt;
    if (has_negative_prompt) {
        TRY_V_AS_HRPC_STATUS(auto negative_prompt_buffer, m_generation_session.read(), Text2ImageGeneratorGenerateSerializer);
        negative_prompt = negative_prompt_buffer->to_string();
        LOGGER__INFO("Negative prompt - {}", negative_prompt);
    }
    if (m_ip_adapter) {
        LOGGER__GENAI_STATS_START("[text2image-generation] handle ip adapter");
        CHECK_SUCCESS_AS_HRPC_STATUS(handle_ip_adapter_frame(), Text2ImageGeneratorGenerateSerializer);
        LOGGER__GENAI_STATS_END("[text2image-generation] handle ip adapter");
    }

    // Check before text encoder
    CHECK_SUCCESS_AS_HRPC_STATUS(check_abort_state(), Text2ImageGeneratorGenerateSerializer);

    LOGGER__GENAI_STATS_START("[text2image-generation] text encoder");
    CHECK_SUCCESS_AS_HRPC_STATUS(m_text_encoder->encode(positive_prompt, negative_prompt), Text2ImageGeneratorGenerateSerializer);
    LOGGER__GENAI_STATS_END("[text2image-generation] text encoder");

    // TODO: Consider optimize by using batch size for samples count
    for (uint32_t i = 0; i < m_generator_params.samples_count(); i++) {
        LOGGER__INFO("Generating sample number {}", i);

        // Check before denoiser
        CHECK_SUCCESS_AS_HRPC_STATUS(check_abort_state(), Text2ImageGeneratorGenerateSerializer);

        // denoise timestamps are logged in the denoiser::execute method
        auto denoiser_status = m_denoiser->execute(m_generator_params, has_negative_prompt);
        CHECK_SUCCESS_AS_HRPC_STATUS(denoiser_status, Text2ImageGeneratorGenerateSerializer);

        // Check before image decoder
        CHECK_SUCCESS_AS_HRPC_STATUS(check_abort_state(), Text2ImageGeneratorGenerateSerializer);

        LOGGER__GENAI_STATS_START("[text2image-generation] image decoder");
        CHECK_SUCCESS_AS_HRPC_STATUS(m_image_decoder->execute(), Text2ImageGeneratorGenerateSerializer);
        TRY_AS_HRPC_STATUS(auto decoder_output, m_image_decoder->get_output_memview(), Text2ImageGeneratorGenerateSerializer);
        LOGGER__GENAI_STATS_END("[text2image-generation] image decoder");


        if (m_has_super_resolution) {
            // Check before super resolution
            CHECK_SUCCESS_AS_HRPC_STATUS(check_abort_state(), Text2ImageGeneratorGenerateSerializer);

            LOGGER__GENAI_STATS_START("[text2image-generation] super resolution and post-process");
            pre_super_resolution_process(decoder_output);
            CHECK_SUCCESS_AS_HRPC_STATUS(m_super_resolution->execute(), Text2ImageGeneratorGenerateSerializer);
            TRY_AS_HRPC_STATUS(auto sr_output, m_super_resolution->get_output_memview(), Text2ImageGeneratorGenerateSerializer);
            post_super_resolution_process(sr_output, MemoryView(m_output_result));
            LOGGER__GENAI_STATS_END("[text2image-generation] super resolution and post-process");
        } else {
            LOGGER__GENAI_STATS_START("[text2image-generation] post-process");
            post_process_output_decoder(decoder_output, MemoryView(m_output_result));
            LOGGER__GENAI_STATS_END("[text2image-generation] post-process");
        }

        // Check before return sample
        CHECK_SUCCESS_AS_HRPC_STATUS(check_abort_state(), Text2ImageGeneratorGenerateSerializer);

        // TODO: HRT-15800 - Split to two messages and write to output queue
        // Return next sample status to client, and then the sample itself
        TRY_AS_HRPC_STATUS(auto sample_reply, Text2ImageGeneratorGenerateSerializer::serialize_reply(HAILO_SUCCESS), Text2ImageGeneratorGenerateSerializer);
        CHECK_SUCCESS_AS_HRPC_STATUS(m_generation_session.write(MemoryView(sample_reply)), Text2ImageGeneratorGenerateSerializer);
        CHECK_SUCCESS_AS_HRPC_STATUS(m_generation_session.write(MemoryView(m_output_result)), Text2ImageGeneratorGenerateSerializer);
    }

    TRY_AS_HRPC_STATUS(auto reply, Text2ImageGeneratorGenerateSerializer::serialize_reply(HAILO_SUCCESS), Text2ImageGeneratorGenerateSerializer);
    LOGGER__INFO("Finished generation");

    return reply;
}

hailo_status Text2ImageServer::run_generation_cycle()
{
    TRY(auto generation_request, m_generation_session.read());
    CHECK(HAILO_TIMEOUT == m_shutdown_event->wait(std::chrono::milliseconds(0)), HAILO_SHUTDOWN_EVENT_SIGNALED, "Shutdown event signaled");

    std::unique_lock<std::mutex> lock(m_generation_mutex);
    m_state->store(State::GENERATING);
    TRY(auto reply, handle_generate_request(MemoryView(*generation_request)));
    CHECK_SUCCESS(m_generation_session.write(MemoryView(reply)));
    m_state->store(State::READY);
    m_cv.notify_all();

    return HAILO_SUCCESS;
}

void Text2ImageServer::init_generation_thread(const std::string &generation_session_listener_ip)
{
    m_generation_thread = std::thread([this, generation_session_listener_ip]()
    {
        auto generation_session_listener = SessionListener::create_shared(genai::DEFAULT_TEXT2IMAGE_GENERATION_CONNECTION_PORT, generation_session_listener_ip);
        CHECK_SUCCESS_OR_DO_AND_RETURN(generation_session_listener.status(), m_state->store(State::ERROR),
            "Failed to create generation session listener with status '{}'", generation_session_listener.status());

        // TODO: HRT-18241 - Add a timeout to the accept() call
        auto generation_session = generation_session_listener.value()->accept();
        CHECK_SUCCESS_OR_DO_AND_RETURN(generation_session.status(), m_state->store(State::ERROR),
            "Failed to accept generation session with status '{}'", generation_session.status());
        m_generation_session = SessionWrapper(generation_session.release());

        while (HAILO_TIMEOUT == m_shutdown_event->wait(std::chrono::milliseconds(0))) {
            auto status = run_generation_cycle();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to run generation cycle with status '{}'", status);
                m_state->store(State::ERROR);
                m_cv.notify_all();
                return;
            }
        }
    });
}

Expected<Buffer> Text2ImageServer::handle_get_generator_params_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(Text2ImageGetGeneratorParamsSerializer::deserialize_request(request), Text2ImageGetGeneratorParamsSerializer);
    TRY_AS_HRPC_STATUS(auto reply, Text2ImageGetGeneratorParamsSerializer::serialize_reply(HAILO_SUCCESS, m_generator_params), Text2ImageGetGeneratorParamsSerializer);
    return reply;
}

Expected<Buffer> Text2ImageServer::handle_tokenize_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto prompt, Text2ImageTokenizeSerializer::deserialize_request(request), Text2ImageTokenizeSerializer);
    TRY_AS_HRPC_STATUS(auto tokens, m_text_encoder->tokenize(prompt), Text2ImageTokenizeSerializer);
    TRY_AS_HRPC_STATUS(auto reply, Text2ImageTokenizeSerializer::serialize_reply(HAILO_SUCCESS, tokens), Text2ImageTokenizeSerializer);
    return reply;
}

Expected<Buffer> Text2ImageServer::handle_set_initial_noise_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(Text2ImageGeneratorSetInitialNoiseSerializer::deserialize_request(request), Text2ImageGeneratorSetInitialNoiseSerializer);
    CHECK_AS_HRPC_STATUS(State::READY == m_state->load(), HAILO_INVALID_OPERATION, Text2ImageGeneratorSetInitialNoiseSerializer);

    auto noise_buffer = m_denoiser->get_initial_noise_buffer();
    TRY_AS_HRPC_STATUS(auto bytes_read, m_session.read(noise_buffer), Text2ImageGeneratorSetInitialNoiseSerializer);
    LOGGER__INFO("Got initial noise");

    // TODO: HRT-17344 - Change to CHECK macro with a msg log
    if (bytes_read != noise_buffer.size()) {
        LOGGER__ERROR("Mismatch of noise size. expected {}, got {}", noise_buffer.size(), bytes_read);
        return Text2ImageGeneratorSetInitialNoiseSerializer::serialize_reply(HAILO_INVALID_ARGUMENT);
    }

    m_denoiser->set_initial_noise_flag(true);
    TRY_AS_HRPC_STATUS(auto reply, Text2ImageGeneratorSetInitialNoiseSerializer::serialize_reply(HAILO_SUCCESS), Text2ImageGeneratorSetInitialNoiseSerializer);
    return reply;
}

hailo_status Text2ImageServer::abort_generation()
{
    if (State::GENERATING == m_state->load()) {
        m_state->store(State::ABORTING);

        // Wait for the generation thread to finish abort and to return the state to ready
        std::unique_lock<std::mutex> lock(m_generation_mutex);
        CHECK(m_cv.wait_for(lock, LONG_TIMEOUT,
            [this] { return (State::READY == m_state->load()) || (State::ERROR == m_state->load()); }), HAILO_TIMEOUT);
        CHECK(State::ERROR != m_state->load(), HAILO_INTERNAL_FAILURE, "Failed to abort generation");
    }
    return HAILO_SUCCESS;
}

Expected<Buffer> Text2ImageServer::handle_abort_request(const MemoryView &request)
{
    CHECK_SUCCESS_AS_HRPC_STATUS(Text2ImageGeneratorAbortSerializer::deserialize_request(request), Text2ImageGeneratorAbortSerializer);
    CHECK_AS_HRPC_STATUS(State::ERROR != m_state->load(), HAILO_INTERNAL_FAILURE, Text2ImageGeneratorAbortSerializer);
    LOGGER__INFO("Got abort request");
    CHECK_SUCCESS_AS_HRPC_STATUS(abort_generation(), Text2ImageGeneratorAbortSerializer);
    TRY_AS_HRPC_STATUS(auto reply, Text2ImageGeneratorAbortSerializer::serialize_reply(HAILO_SUCCESS), Text2ImageGeneratorAbortSerializer);
    return reply;
}

Text2ImageServer::Text2ImageServer(std::shared_ptr<Session> session, std::shared_ptr<Event> shutdown_event,
    std::shared_ptr<std::atomic<State>> state, const std::string &generation_session_listener_ip) :
        m_session(SessionWrapper(session)), m_generation_session(SessionWrapper(nullptr)), m_state(state),
        m_shutdown_event(shutdown_event), m_has_super_resolution(false)
{
    init_generation_thread(generation_session_listener_ip);
}

Text2ImageServer::~Text2ImageServer()
{
    LOGGER__INFO("Releasing Text2ImageServer");

    auto status = abort_generation();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to abort generation");
    }

    m_shutdown_event->signal();

    // We close the generation session to ensure it exits the blocking read() call in the generation thread.
    // Only after exiting read() will the thread detect the shutdown event and terminate.
    status = m_generation_session.close();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to close generation session");
    }

    if (m_generation_thread.joinable()) {
        m_generation_thread.join();
    }

    if (m_text_encoder) {
        m_text_encoder.reset();
    }
    if (m_denoiser) {
        m_denoiser.reset();
    }
    if (m_image_decoder) {
        m_image_decoder.reset();
    }
    if (m_ip_adapter) {
        m_ip_adapter.reset();
    }
    if (m_super_resolution) {
        m_super_resolution.reset();
    }

}

Expected<std::unique_ptr<Text2ImageServerManager>> Text2ImageServerManager::create(std::shared_ptr<Session> session, const std::string &generation_session_listener_ip)
{
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

    auto state = make_shared_nothrow<std::atomic<Text2ImageServer::State>>(Text2ImageServer::State::READY);
    CHECK_NOT_NULL_AS_EXPECTED(state, HAILO_OUT_OF_HOST_MEMORY);

    auto server = make_unique_nothrow<Text2ImageServer>(session, shutdown_event, state, generation_session_listener_ip);
    CHECK_NOT_NULL_AS_EXPECTED(server, HAILO_OUT_OF_HOST_MEMORY);

    auto server_manager = make_unique_nothrow<Text2ImageServerManager>(session, std::move(server));
    CHECK_NOT_NULL_AS_EXPECTED(server_manager, HAILO_OUT_OF_HOST_MEMORY);

    return server_manager;
}

Text2ImageServerManager::Text2ImageServerManager(std::shared_ptr<Session> session, std::unique_ptr<Text2ImageServer> &&server) :
    GenAIServerManager(session), m_server(std::move(server))
{
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__CREATE] =
        [&](const MemoryView &request) { return m_server->handle_create_text2image_request(request); };
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__GET_GENERATOR_PARAMS] =
        [&](const MemoryView &request) { return m_server->handle_get_generator_params_request(request); };
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__GENERATOR_CREATE] =
        [&](const MemoryView &request) { return m_server->handle_create_generator_request(request); };
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__GET_IP_ADAPTER_FRAME_INFO] =
        [&](const MemoryView &request) { return m_server->handle_get_ip_adapter_frame_info_request(request); };
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__TOKENIZE] =
        [&](const MemoryView &request) { return m_server->handle_tokenize_request(request); };
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__RELEASE] =
        [&](const MemoryView &request) { (void)request; m_server.reset(); return Text2ImageReleaseSerializer::serialize_reply(HAILO_SUCCESS); };
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__GENERATOR_SET_INITIAL_NOISE] =
        [&](const MemoryView &request) { return m_server->handle_set_initial_noise_request(request); };
    m_dispatcher[HailoGenAIActionID::TEXT2IMAGE__GENERATOR_ABORT] =
        [&](const MemoryView &request) { return m_server->handle_abort_request(request); };
}


} /* namespace genai */
} /* namespace hailort */