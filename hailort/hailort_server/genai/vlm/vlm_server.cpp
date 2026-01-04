/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm_server.cpp
 * @brief Implementation for VLM server
 **/

#include "vlm_server.hpp"

#include "hailo/hailort_defaults.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/quantization.hpp"

#include "common/file_utils.hpp"
#include "common/genai/constants.hpp"

#include "utils.hpp"

#include <nlohmann/json.hpp>
#include <future>


using namespace hailort::genai;
using namespace hailort;

static const std::string FRAME_ENCODER_MODEL_NAME_SUFFIX = "encoder";

static const std::string ENCODER_FIRST_INPUT_NAME_SUFF = "input_layer1";
static const std::string ENCODER_SECOND_INPUT_NAME_SUFF = "input_layer2";

Expected<std::unique_ptr<LLMServer>> VLMServer::create_unique(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    // Init with generation default params, will be overwritten by the params from the HEF
    auto post_process_params = LLMGeneratorParams(VLMServer::DEFAULT_GENERATION_TEMPERATURE, VLMServer::DEFAULT_GENERATION_TOP_P,
        VLMServer::DEFAULT_GENERATION_TOP_K, VLMServer::DEFAULT_GENERATION_FREQ_PENALTY, LLMServer::DEFAULT_GENERATION_MAX_GENERATED_TOKENS,
        VLMServer::DEFAULT_GENERATION_DO_SAMPLE, HAILO_RANDOM_SEED);

    auto ptr = std::make_unique<VLMServer>(session, vdevice_manager, std::move(post_process_params));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMServer>(std::move(ptr));
}

VLMServer::VLMServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager, LLMGeneratorParams &&post_process_params) :
        LLMServer(session, vdevice_manager, std::move(post_process_params))
{
}

hailo_status VLMServer::parse_config_json(const MemoryView &config_json)
{
    auto json_ptr = config_json.data();
    auto json_size = config_json.size();
    auto hailo_config_json = nlohmann::json::parse(json_ptr, json_ptr + json_size);

    CHECK(hailo_config_json.contains("image_pad"), HAILO_INVALID_ARGUMENT);
    m_image_pad_token_id = hailo_config_json["image_pad"].get<int>();
    m_video_pad_token_id = 151656; // "<|video_pad|>" - default value for qwen family models
    if (hailo_config_json.contains("video_pad")) {
        m_video_pad_token_id = hailo_config_json["video_pad"].get<int>();
    }

    return LLMServer::parse_config_json(hailo_config_json);
}

std::future<hailo_status> VLMServer::create_pre_process_future(const Hef &hef,
    std::shared_ptr<Event> inference_models_created_event, std::future<Expected<Eigen::VectorXf>> &external_resources_future,
    std::shared_ptr<Event> pre_process_created_event, std::shared_ptr<Event> shutdown_event)
{
    return std::async(std::launch::async, [this, hef, inference_models_created_event, &external_resources_future, pre_process_created_event, shutdown_event]() -> hailo_status {
        CHECK_SUCCESS(WaitOrShutdown(inference_models_created_event, shutdown_event).wait(WAIT_FOR_OPERATION_TIMEOUT));
        TRY(auto theta, wait_for_future_value(external_resources_future));

        LOGGER__GENAI_STATS_START("[create] create PreProcess");
        auto prefill_inputs_frame_size = m_inference_manager_prefill->get_inputs_frame_size();
        auto tbt_inputs_frame_size = m_inference_manager_tbt ? m_inference_manager_tbt->get_inputs_frame_size() : std::map<std::string, size_t>();
        TRY(auto embeddings_input_name, get_layer_name_from_suffix<size_t>(m_input_layers_names_suffixes.embeddings, prefill_inputs_frame_size));
        TRY(auto embeddings_input, m_inference_manager_prefill->get_model()->input(embeddings_input_name));
        m_embeddings_features = embeddings_input.shape().features;

        // Get scaled-mask value
        TRY(auto mask_input_name, get_layer_name_from_suffix<size_t>(m_input_layers_names_suffixes.attention_mask, prefill_inputs_frame_size));
        TRY(auto mask_input, m_inference_manager_prefill->get_model()->input(mask_input_name));
        auto mask_quant_infos = mask_input.get_quant_infos();
        CHECK(1 == mask_quant_infos.size(), HAILO_INTERNAL_FAILURE);
        float32_t dequantized_mask_value = 1;
        uint8_t scaled_mask_value = 0;
        Quantization::quantize_input_buffer<float32_t, uint8_t>(&dequantized_mask_value, &scaled_mask_value, 1, mask_quant_infos[0]);

        // Create VLMPreProcess (different from LLM - needs encoder_input_shape)
        TRY(m_pre_process, VLMPreProcess::create(prefill_inputs_frame_size, tbt_inputs_frame_size,
            std::move(theta), m_embeddings_features, m_encoder_input_shape, scaled_mask_value, m_input_layers_names_suffixes, m_pre_process_params));
        LOGGER__GENAI_STATS_END("[create] create PreProcess");
        pre_process_created_event->signal();

        return HAILO_SUCCESS;
    });
}

std::future<hailo_status> VLMServer::create_token_embedder_future(const Hef &hef,
    std::shared_ptr<Event> embeddings_arrived_event, std::shared_ptr<Event> pre_process_created_event, std::shared_ptr<Event> shutdown_event)
{
    return std::async(std::launch::async, [this, hef, embeddings_arrived_event, pre_process_created_event, shutdown_event]() -> hailo_status {
        CHECK_SUCCESS(WaitOrShutdown(pre_process_created_event, shutdown_event).wait(WAIT_FOR_OPERATION_TIMEOUT));
        CHECK_SUCCESS(WaitOrShutdown(embeddings_arrived_event, shutdown_event).wait(LONG_TIMEOUT)); // Waiting for data over the session

        LOGGER__GENAI_STATS_START("[create] create token embedder");
        const auto embeddings_per_frame = dynamic_cast<VLMPreProcess*>(m_pre_process.get())->embeddings_per_frame();
        TRY(auto embeddings_view, hef.get_external_resources(INPUT_EMB_BINARY));

        TRY(m_token_embedder, TokenEmbedder<uint16_t>::create(embeddings_view,
            embeddings_view.size() / (sizeof(uint16_t) * m_embeddings_features), m_embeddings_features,
            m_image_pad_token_id, m_video_pad_token_id, embeddings_per_frame));
        LOGGER__GENAI_STATS_END("[create] create token embedder");

        return HAILO_SUCCESS;
    });
}

// VLM-specific: Create frame encoder asynchronously
std::future<hailo_status> VLMServer::create_frame_encoder_future(std::shared_ptr<VDevice> vdevice, const Hef &hef,
    std::shared_ptr<Event> frame_encoder_created_event)
{
    return std::async(std::launch::async, [this, vdevice, hef, frame_encoder_created_event]() -> hailo_status {
        LOGGER__GENAI_STATS_START("[create] create vision encoder model");
        TRY(auto frame_encoder_model_name, get_model_name_from_suffix(hef, FRAME_ENCODER_MODEL_NAME_SUFFIX));
        TRY(m_inference_manager_frame_encoder, InferenceManager::create(vdevice, hef, frame_encoder_model_name));

        auto model_encoder = m_inference_manager_frame_encoder->get_model();
        auto &encoder_input_config = *model_encoder->inputs().begin(); // All inputs are the same shape

        // Store encoder input shape for VLMPreProcess creation
        m_encoder_input_shape = encoder_input_config.shape();
        frame_encoder_created_event->signal();  // Signal that encoder_input_shape is ready
        LOGGER__GENAI_STATS_END("[create] create vision encoder model");

        // Configure vision encoder
        LOGGER__GENAI_STATS_START("[create] configure vision encoder model");
        CHECK_SUCCESS(m_inference_manager_frame_encoder->configure());
        LOGGER__GENAI_STATS_END("[create] configure vision encoder model");

        return HAILO_SUCCESS;
    });
}

Expected<std::future<hailo_status>> VLMServer::create_resources_async(std::shared_ptr<VDevice> vdevice, std::shared_ptr<Buffer> hef_buffer,
    bool tokenizer_on_host, std::shared_ptr<Event> theta_arrived_event, std::shared_ptr<Event> hailo_config_json_arrived_event,
    std::shared_ptr<Event> tokenizer_arrived_event, std::shared_ptr<Event> embeddings_arrived_event, std::shared_ptr<Event> shutdown_event)
{
    TRY(auto inference_models_created_event , Event::create_shared(Event::State::not_signalled));
    TRY(auto external_resources_created_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto pre_process_created_event, Event::create_shared(Event::State::not_signalled));
    TRY(auto frame_encoder_created_event, Event::create_shared(Event::State::not_signalled));

    return std::async(std::launch::async, [this, vdevice, hef_buffer, tokenizer_on_host, theta_arrived_event,
        hailo_config_json_arrived_event, tokenizer_arrived_event, embeddings_arrived_event, inference_models_created_event,
        external_resources_created_event, pre_process_created_event, frame_encoder_created_event, shutdown_event]() -> hailo_status {

        // Create HEF
        LOGGER__GENAI_STATS_START("[create] create HEF");
        TRY(auto hef, Hef::create(hef_buffer));
        hef.set_memory_footprint_optimization(true);
        LOGGER__GENAI_STATS_END("[create] create HEF");

        // Spawn all async creation tasks
        auto external_resources_future = parse_external_resources_future(hef, hailo_config_json_arrived_event,
            theta_arrived_event, external_resources_created_event, shutdown_event);

        auto inference_managers_future = create_inference_managers_future(vdevice, hef, "",
            external_resources_created_event, inference_models_created_event, shutdown_event);

        auto frame_encoder_future = create_frame_encoder_future(vdevice, hef, frame_encoder_created_event);

        // Wait for frame encoder to set m_encoder_input_shape before creating PreProcess
        CHECK_SUCCESS(WaitOrShutdown(frame_encoder_created_event, shutdown_event).wait(WAIT_FOR_OPERATION_TIMEOUT));

        auto pre_process_future = create_pre_process_future(hef, inference_models_created_event, external_resources_future, pre_process_created_event, shutdown_event);

        if (!tokenizer_on_host) {
            auto tokenizer_future = create_tokenizer_future(hef, tokenizer_arrived_event, shutdown_event);
            auto token_embedder_future = create_token_embedder_future(hef,
                embeddings_arrived_event, pre_process_created_event, shutdown_event);
            CHECK_SUCCESS(wait_for_future_status_or_shutdown(tokenizer_future, shutdown_event));
            CHECK_SUCCESS(wait_for_future_status_or_shutdown(token_embedder_future, shutdown_event));
        }

        // Wait for all async tasks to complete
        CHECK_SUCCESS(wait_for_future_status_or_shutdown(pre_process_future, shutdown_event));
        CHECK_SUCCESS(wait_for_future_status_or_shutdown(inference_managers_future, shutdown_event));
        CHECK_SUCCESS(wait_for_future_status_or_shutdown(frame_encoder_future, shutdown_event));

        return HAILO_SUCCESS;
    });
}

Expected<Buffer> VLMServer::handle_create_vlm_request(const MemoryView &request)
{
    LOGGER__GENAI_STATS_START("[create] create vdevice");
    TRY_AS_HRPC_STATUS(auto tuple, VLMCreateSerializer::deserialize_request(request), VLMCreateSerializer);
    auto &group_id = std::get<0>(tuple);
    auto &hef_path = std::get<1>(tuple);
    auto &chunks_to_transfer = std::get<2>(tuple);
    auto tokenizer_on_host = std::get<3>(tuple);
    auto total_hef_size = std::get<4>(tuple);

    auto params = HailoRTDefaults::get_vdevice_params();
    if (!group_id.empty()) {
        params.group_id = group_id.c_str();
    }
    TRY_AS_HRPC_STATUS(auto vdevice, m_vdevice_manager->create_shared_vdevice(params, DEFAULT_LLM_CONNECTION_PORT), VLMCreateSerializer);
    LOGGER__GENAI_STATS_END("[create] create vdevice");

    LOGGER__GENAI_STATS_START("[create] transfer HEF");
    std::shared_ptr<Buffer> hef_buffer_ptr;
    std::future<hailo_status> resources_creation_future;
    std::future<hailo_status> ccws_future;
    TRY_AS_HRPC_STATUS(auto theta_arrived_event, Event::create_shared(Event::State::not_signalled), VLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto hailo_config_json_arrived_event, Event::create_shared(Event::State::not_signalled), VLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto tokenizer_arrived_event, Event::create_shared(Event::State::not_signalled), VLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto embeddings_arrived_event, Event::create_shared(Event::State::not_signalled), VLMCreateSerializer);
    TRY_AS_HRPC_STATUS(auto shutdown_event, Event::create_shared(Event::State::not_signalled), VLMCreateSerializer);

    // Create EventGuard AFTER futures are declared but BEFORE any operations that might fail
    // This ensures that if any error occurs, shutdown_event will be signaled on early return
    // and any already-launched futures will be notified to stop waiting
    EventGuard event_guard(shutdown_event);

    if (!hef_path.empty()) { // hef path is not none only if hef exists locally, so no need to transfer it over the session
        TRY_AS_HRPC_STATUS(auto buff, read_binary_file(hef_path, BufferStorageParams::create_dma()), VLMCreateSerializer);
        hef_buffer_ptr = make_shared_nothrow<Buffer>(std::move(buff));
        CHECK_AS_HRPC_STATUS(nullptr != hef_buffer_ptr, HAILO_OUT_OF_HOST_MEMORY, VLMCreateSerializer);

        // For local HEF, create resources immediately in async task
        TRY_AS_HRPC_STATUS(resources_creation_future, create_resources_async(vdevice, hef_buffer_ptr, tokenizer_on_host, theta_arrived_event,
            hailo_config_json_arrived_event, tokenizer_arrived_event, embeddings_arrived_event, shutdown_event), VLMCreateSerializer);
        // Since all data is already in the buffer, signal the events
        theta_arrived_event->signal();
        hailo_config_json_arrived_event->signal();
        tokenizer_arrived_event->signal();
        embeddings_arrived_event->signal();
    } else {
        // Use total HEF size from the request
        LOGGER__INFO("hef buffer of size '{}'", total_hef_size);
        TRY_AS_HRPC_STATUS(hef_buffer_ptr, Buffer::create_shared(total_hef_size, BufferStorageParams::create_dma()), VLMCreateSerializer);

        // Receive all chunks synchronously, spawning async creation tasks after key chunks arrive
        for (const auto &chunk : chunks_to_transfer) {
            // Receive all chunks synchronously, except for CCWs
            if (chunk.name != CCWS) {
                CHECK_SUCCESS_AS_HRPC_STATUS(receive_hef_chunk_sync(m_session, chunk, hef_buffer_ptr),
                VLMCreateSerializer);
            } else {
                ccws_future = std::async(std::launch::async, [this, &chunk, &hef_buffer_ptr]() -> hailo_status {
                    LOGGER__INFO("Receiving CCWs chunk '{}' (offset: {}, size: {} bytes) [ASYNC]", chunk.name, chunk.offset, chunk.size);
                    auto status = receive_hef_chunk_sync(m_session, chunk, hef_buffer_ptr);
	                LOGGER__GENAI_STATS_END("[create] transfer HEF");
                    return status;
                });
            }
            // After receiving HEADER_PROTO_PADDING, start HEF creation asynchronously
            if (chunk.name == HEADER_PROTO_PADDING) {
                LOGGER__INFO("{} received, starting async resources creation", HEADER_PROTO_PADDING);
                TRY_AS_HRPC_STATUS(resources_creation_future, create_resources_async(vdevice, hef_buffer_ptr, tokenizer_on_host,
                    theta_arrived_event, hailo_config_json_arrived_event, tokenizer_arrived_event, embeddings_arrived_event, shutdown_event), VLMCreateSerializer);
            } else if (chunk.name == HAILO_CONFIG_JSON) {
                hailo_config_json_arrived_event->signal();
            } else if (chunk.name == THETA) {
                theta_arrived_event->signal();
            } else if (chunk.name == INPUT_EMB_BINARY) {
                embeddings_arrived_event->signal();
            } else if (chunk.name == TOKENIZER) {
                tokenizer_arrived_event->signal();
            }
        }
    }

    CHECK_SUCCESS_AS_HRPC_STATUS(wait_for_future_status(resources_creation_future), VLMCreateSerializer);
    if (ccws_future.valid()) { // In case HEF exists locally, dont need to wait for CCWs
        CHECK_SUCCESS_AS_HRPC_STATUS(wait_for_future_status(ccws_future), VLMCreateSerializer);
    }

    m_recovery.tokens = {m_end_of_sentence_token_id};

    // Get encoder input config from the created frame encoder
    auto model_encoder = m_inference_manager_frame_encoder->get_model();
    auto &encoder_input_config = *model_encoder->inputs().begin(); // All inputs are the same format and shape
    hailo_format_t input_frame_format = encoder_input_config.format();
    hailo_3d_image_shape_t input_frame_shape = encoder_input_config.shape();

    // Get embeddings_per_frame from VLMPreProcess
    const auto embeddings_per_frame = dynamic_cast<VLMPreProcess*>(m_pre_process.get())->embeddings_per_frame();

    TRY_AS_HRPC_STATUS(auto reply, VLMCreateSerializer::serialize_reply(HAILO_SUCCESS, input_frame_shape, input_frame_format, m_chat_template, m_embeddings_features,
        m_image_pad_token_id, m_video_pad_token_id, embeddings_per_frame), VLMCreateSerializer);

    return reply;
}

Expected<Buffer> VLMServer::handle_vlm_generate_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto request_info, VLMGeneratorGenerateSerializer::deserialize_request(request),
        VLMGeneratorGenerateSerializer);
    auto &[number_of_standalone_frames, raw_video_frames_count_per_video] = request_info;

    // Compute total number of video frames from the per-video counts
    uint32_t number_of_video_frames = std::accumulate(raw_video_frames_count_per_video.begin(), raw_video_frames_count_per_video.end(), 0u);

    auto total_number_of_frames = number_of_standalone_frames + number_of_video_frames;

    std::unique_lock<std::mutex> lock(m_generation_mutex);

    LOGGER__INFO("Generate request received with {} frames ({} standalone, {} video)", total_number_of_frames, number_of_standalone_frames, number_of_video_frames);

    prepare_for_new_generation();

    TRY(auto encoder_output_config, m_inference_manager_frame_encoder->get_model()->output());
    auto encoder_output_frame_size = encoder_output_config.get_frame_size();
    std::vector<BufferPtr> frame_encoder_input_buffers;
    frame_encoder_input_buffers.reserve(total_number_of_frames);
    // TODO (HRT-19393): Optimize the buffers allocations (pool? lazy allocation?)

    // Handle standalone frames
    for (uint32_t i = 0; i < number_of_standalone_frames; i++) {
        TRY_AS_HRPC_STATUS(auto encoder_input_frame, m_session.read(), VLMGeneratorGenerateSerializer);
        frame_encoder_input_buffers.push_back(encoder_input_frame);
        TRY_AS_HRPC_STATUS(auto encoder_output_frame, Buffer::create_shared(encoder_output_frame_size, BufferStorageParams::create_dma()), VLMGeneratorGenerateSerializer);
        m_current_standalone_frames_embeddings.push_back(encoder_output_frame);
    }

    // Process frames to get frame embeddings - TODO: HRT-17264 - Move to async generation
    for (uint32_t i = 0; i < number_of_standalone_frames; i++) {
        LOGGER__GENAI_STATS_START("[generate-prefill] encode frame");
        std::map<std::string, MemoryView> inputs;
        for (auto &input_config : m_inference_manager_frame_encoder->get_model()->inputs()) {
            inputs[input_config.name()] = MemoryView(frame_encoder_input_buffers[i]);
        }
        std::map<std::string, MemoryView> outputs {{encoder_output_config.name(), MemoryView(m_current_standalone_frames_embeddings[i])}};
        CHECK_SUCCESS_AS_HRPC_STATUS(m_inference_manager_frame_encoder->generate(inputs, outputs), VLMGeneratorGenerateSerializer);
        LOGGER__GENAI_STATS_END("[generate-prefill] encode frame");
    }

    // Handle video frames
    // Raise errors in case the encoder has only 1 input and video frames are passed
    if (0 != number_of_video_frames) {
        CHECK_AS_HRPC_STATUS(m_inference_manager_frame_encoder->get_model()->inputs().size() == 2, HAILO_INVALID_OPERATION, VLMGeneratorGenerateSerializer);
    }

    for (uint32_t i = 0; i < number_of_video_frames; i++) {
        TRY_AS_HRPC_STATUS(auto encoder_input_frame, m_session.read(), VLMGeneratorGenerateSerializer);
        frame_encoder_input_buffers.push_back(encoder_input_frame);
    }
    uint32_t number_of_video_frames_to_process = 0;
    // Build vector of processed frame counts per video (encoder outputs half the input frames, rounded up)
    std::vector<size_t> processed_video_frames_count_per_video;
    processed_video_frames_count_per_video.reserve(raw_video_frames_count_per_video.size());
    for (auto raw_count : raw_video_frames_count_per_video) {
        uint32_t processed_count = (raw_count + 1) / 2;
        number_of_video_frames_to_process += processed_count;
        processed_video_frames_count_per_video.push_back(processed_count);
    }
    if (m_token_embedder) {
        m_token_embedder->set_video_frames_count(processed_video_frames_count_per_video);
    }

    m_current_videos_embeddings.reserve(number_of_video_frames_to_process);
    for (uint32_t i = 0; i < number_of_video_frames_to_process; i++) {
        TRY_AS_HRPC_STATUS(auto encoder_output_frame, Buffer::create_shared(encoder_output_frame_size, BufferStorageParams::create_dma()), VLMGeneratorGenerateSerializer);
        m_current_videos_embeddings.push_back(encoder_output_frame);

        auto input_index_first = number_of_standalone_frames + (2 * i);
        auto input_index_second = ((2 * i) + 1 < number_of_video_frames) ? input_index_first + 1 :
            input_index_first;

        LOGGER__GENAI_STATS_START("[generate-prefill] encode frames");
        std::map<std::string, MemoryView> inputs;
        for (auto &input_config : m_inference_manager_frame_encoder->get_model()->inputs()) {
            if (has_suffix(input_config.name(), ENCODER_FIRST_INPUT_NAME_SUFF)) {
                inputs[input_config.name()] = MemoryView(frame_encoder_input_buffers[input_index_first]);
            } else if (has_suffix(input_config.name(), ENCODER_SECOND_INPUT_NAME_SUFF)) {
                inputs[input_config.name()] = MemoryView(frame_encoder_input_buffers[input_index_second]);
            } else {
                LOGGER__ERROR("Invalid input config name: '{}' for video processing - expecting suffixes '{}' or '{}'",
                    input_config.name(), ENCODER_FIRST_INPUT_NAME_SUFF, ENCODER_SECOND_INPUT_NAME_SUFF);
                CHECK_AS_HRPC_STATUS(false, HAILO_INVALID_ARGUMENT, VLMGeneratorGenerateSerializer);
            }
        }
        std::map<std::string, MemoryView> outputs {{encoder_output_config.name(), MemoryView(m_current_videos_embeddings[i])}};
        auto status = m_inference_manager_frame_encoder->generate(inputs, outputs);
        LOGGER__GENAI_STATS_END("[generate-prefill] encode frames");
        CHECK_AS_HRPC_STATUS(status == HAILO_SUCCESS, status, VLMGeneratorGenerateSerializer);
    }

    TRY_AS_HRPC_STATUS(auto generator_generate_reply, VLMGeneratorGenerateSerializer::serialize_reply(HAILO_SUCCESS),
        VLMGeneratorGenerateSerializer);
    return generator_generate_reply;
}

hailo_status VLMServer::process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<EmbeddingViewWrapper> &input_embeddings,
    EmbeddingsVectorState &standalone_frame_embeddings_state, EmbeddingsVectorState &video_embeddings_state)
{
    LOGGER__GENAI_STATS_START("[generate-prefill] pre process");
    CHECK_SUCCESS(dynamic_cast<VLMPreProcess*>(m_pre_process.get())->prepare_inputs_prefill(prefill_inputs, input_embeddings,
        standalone_frame_embeddings_state, video_embeddings_state));
    LOGGER__GENAI_STATS_END("[generate-prefill] pre process");

    LOGGER__GENAI_STATS_START("[generate-prefill] update cache offset");
    CHECK_SUCCESS(m_inference_manager_prefill->update_cache_offset(static_cast<int32_t>(input_embeddings.size())));

    LOGGER__GENAI_STATS_END("[generate-prefill] update cache offset");

    LOGGER__GENAI_STATS_START("[generate-prefill] hw-inference prefill");
    CHECK_SUCCESS(m_inference_manager_prefill->generate(prefill_inputs, prefill_outputs));
    LOGGER__GENAI_STATS_END("[generate-prefill] hw-inference prefill");

    return HAILO_SUCCESS;
}

Expected<int> VLMServer::get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
    std::map<std::string, MemoryView> &prefill_outputs, const std::vector<EmbeddingViewWrapper> &input_embeddings,
    const std::vector<BufferPtr> &standalone_frame_embeddings, const std::vector<BufferPtr> &video_embeddings,
    const LLMGeneratorParams &params)
{
    size_t num_full_chunks = input_embeddings.size() / m_pre_process_params.prefill_input_tokens_count;
    size_t remainder_size = input_embeddings.size() % m_pre_process_params.prefill_input_tokens_count;

    EmbeddingsVectorState standalone_frame_embeddings_state(standalone_frame_embeddings, dynamic_cast<VLMPreProcess*>(m_pre_process.get())->embeddings_per_frame());
    EmbeddingsVectorState video_embeddings_state(video_embeddings, dynamic_cast<VLMPreProcess*>(m_pre_process.get())->embeddings_per_frame());

    // Process the remainder first, if any
    if (remainder_size > 0) {
        std::vector<EmbeddingViewWrapper> first_prefill_embeddings(input_embeddings.begin(), input_embeddings.begin() + remainder_size);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, first_prefill_embeddings,
            standalone_frame_embeddings_state, video_embeddings_state));
    }

    // Process full prefill chunks
    size_t offset = remainder_size;
    for (size_t i = 0; i < num_full_chunks; ++i) {
        std::vector<EmbeddingViewWrapper> input_embeddings_chunk(input_embeddings.begin() + offset, input_embeddings.begin() + offset + m_pre_process_params.prefill_input_tokens_count);
        CHECK_SUCCESS(process_prefill_inputs_chunk(prefill_inputs, prefill_outputs, input_embeddings_chunk,
            standalone_frame_embeddings_state, video_embeddings_state));
        offset += input_embeddings_chunk.size();
    }

    LOGGER__GENAI_STATS_START("[generate-prefill] post process");
    auto next_token = m_post_process.get_next_token(prefill_outputs.begin()->second, m_tokens_history, params);
    LOGGER__GENAI_STATS_END("[generate-prefill] post process");

    return next_token;
}


Expected<std::pair<int, LLMGeneratorCompletion::Status>> VLMServer::handle_prefill_phase(const std::vector<int> &tokens,
    const std::vector<EmbeddingViewWrapper> &embeddings)
{
    // VLM prefill phase: process input embeddings WITH frame embeddings and video embeddings
    // Note: m_current_standalone_frames_embeddings and m_current_videos_embeddings should be already populated

    // Use provided embeddings if available (client-side tokenizer), otherwise tokenize on server
    // find the number of image pad tokens and video-pad tokens in the embeddings-vector,
    // and check that they match the number of frames and video embeddings generated by the encoder
    (void)tokens;

    const auto embeddings_per_frame = dynamic_cast<VLMPreProcess*>(m_pre_process.get())->embeddings_per_frame();
    const auto number_of_standalone_frames_embeddings = std::count_if(embeddings.begin(), embeddings.end(), [](auto embedding) {
        return embedding.type() == EmbeddingViewWrapper::EmbeddingType::IMAGE;
    });

    assert(0 == (number_of_standalone_frames_embeddings % embeddings_per_frame));
    CHECK(static_cast<size_t>(number_of_standalone_frames_embeddings / embeddings_per_frame) == m_current_standalone_frames_embeddings.size(),
        HAILO_INVALID_OPERATION,
        "Number of image-pad embeddings from prompt ({}) does not match the number of frames embeddings generated by the encoder ({})",
            (number_of_standalone_frames_embeddings / embeddings_per_frame), m_current_standalone_frames_embeddings.size());

    const auto number_of_video_embeddings = std::count_if(embeddings.begin(), embeddings.end(), [](auto embedding) {
        return embedding.type() == EmbeddingViewWrapper::EmbeddingType::VIDEO;
    });

    assert(0 == (number_of_video_embeddings % embeddings_per_frame));
    CHECK(static_cast<size_t>(number_of_video_embeddings / embeddings_per_frame) == m_current_videos_embeddings.size(), HAILO_INVALID_OPERATION,
        "Number of video-pad embeddings from prompt ({}) does not match the number of video embeddings generated by the encoder ({})",
            (number_of_video_embeddings / embeddings_per_frame), m_current_videos_embeddings.size());

    TRY(auto next_token, get_next_token_prefill(m_prefill_inputs, m_prefill_outputs,
        embeddings, m_current_standalone_frames_embeddings, m_current_videos_embeddings, m_current_generation_params));

    m_current_standalone_frames_embeddings.clear();
    m_current_videos_embeddings.clear();
    m_generated_token_count++;

    auto generation_status = get_current_generation_status(next_token);
    if (generation_status != LLMGeneratorCompletion::Status::GENERATING) {
        // Handle end of generation - may return GENERATING if recovery tokens need delivery
        TRY(generation_status, handle_generation_completion(generation_status, next_token));
    }

    // If no tokens are expected back, override the generated token with 'INVALID_TOKEN_VALUE' - which will be ignored along the way
    if (0 == m_current_generation_params.max_generated_tokens()) {
        next_token = INVALID_TOKEN_VALUE;
    }
    return std::make_pair(next_token, generation_status);
}

Expected<std::unique_ptr<LLMServerManager>> VLMServerManager::create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager)
{
    // Check if KV-Cache is already in use
    CHECK_SUCCESS(vdevice_manager->mark_kv_cache_in_use(), "Failed to acquire KV-Cache. KV-Cache is already in use by another model!");

    auto server = VLMServer::create_unique(session, vdevice_manager);
    if (server.status() != HAILO_SUCCESS) {
        vdevice_manager->unmark_kv_cache_in_use();
    }
    CHECK_EXPECTED(server);

    auto ptr = std::make_unique<VLMServerManager>(session, std::move(server.value()));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return std::unique_ptr<LLMServerManager>(std::move(ptr));
}

VLMServerManager::VLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server) :
    LLMServerManager(session, std::move(server))
{
    m_dispatcher[HailoGenAIActionID::VLM__CREATE] =
        [&](const MemoryView &request) { return dynamic_cast<VLMServer*>(m_server.get())->handle_create_vlm_request(request); };
    m_dispatcher[HailoGenAIActionID::VLM__GENERATOR_GENERATE] =
        [&](const MemoryView &request) { return dynamic_cast<VLMServer*>(m_server.get())->handle_vlm_generate_request(request); };
}