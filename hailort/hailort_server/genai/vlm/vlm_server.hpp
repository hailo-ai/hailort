/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm_server.hpp
 * @brief Implementation for VLM server
 **/

#ifndef _HAILO_HAILO_GENAI_VLM_SERVER_HPP_
#define _HAILO_HAILO_GENAI_VLM_SERVER_HPP_


#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/hailo_session.hpp"
#include "common/utils.hpp"
#include "common/genai/constants.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"

#include "pre_process.hpp"
#include "llm/llm_inference_manager.hpp"
#include "llm/llm_server.hpp"

namespace hailort
{
namespace genai
{

class VLMServer : public LLMServer
{
public:
    static constexpr float32_t DEFAULT_GENERATION_TEMPERATURE = 0.01f;
    static constexpr float32_t DEFAULT_GENERATION_TOP_P = 0.1f;
    static constexpr uint32_t DEFAULT_GENERATION_TOP_K = 1;
    static constexpr float32_t DEFAULT_GENERATION_FREQ_PENALTY = 1.0f;
    static constexpr bool DEFAULT_GENERATION_DO_SAMPLE = false;

    static Expected<std::unique_ptr<LLMServer>> create_unique(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager);

    VLMServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager, LLMGeneratorParams &&post_process_params);
    virtual ~VLMServer() = default;

    // Handlers - consider moving to separate class
    Expected<Buffer> handle_create_vlm_request(const MemoryView &request);
    Expected<Buffer> handle_vlm_generate_request(const MemoryView &request);

protected:
    // Override to create VLMPreProcess instead of LLMPreProcess
    std::future<hailo_status> create_pre_process_future(const Hef &hef,
        std::shared_ptr<Event> inference_models_created_event, std::future<Expected<Eigen::VectorXf>> &external_resources_future,
        std::shared_ptr<Event> pre_process_created_event, std::shared_ptr<Event> shutdown_event) override;

    // Override to create VLM-specific token embedder with image_pad_token and embeddings_per_frame
    std::future<hailo_status> create_token_embedder_future(const Hef &hef,
        std::shared_ptr<Event> embeddings_arrived_event, std::shared_ptr<Event> pre_process_created_event, std::shared_ptr<Event> shutdown_event) override;

    std::future<hailo_status> create_frame_encoder_future(std::shared_ptr<VDevice> vdevice, const Hef &hef,
        std::shared_ptr<Event> frame_encoder_created_event, std::shared_ptr<Event> external_resources_created_event,
        std::shared_ptr<Event> shutdown_event);
    Expected<std::future<hailo_status>> create_resources_async(std::shared_ptr<VDevice> vdevice, std::shared_ptr<Buffer> hef_buffer,
        bool tokenizer_on_host, std::shared_ptr<Event> theta_arrived_event, std::shared_ptr<Event> hailo_config_json_arrived_event,
        std::shared_ptr<Event> tokenizer_arrived_event, std::shared_ptr<Event> embeddings_arrived_event, std::shared_ptr<Event> shutdown_event);

private:
    hailo_status parse_config_json(const MemoryView &config_json) override;

    // Override prefill phase to handle frame embeddings
    Expected<std::pair<int, LLMGeneratorCompletion::Status>> handle_prefill_phase(const std::vector<int> &tokens,
        const std::vector<EmbeddingViewWrapper> &embeddings) override;

    // This function is used to process the prefill inputs and outputs, without handling (exporting) the generated token
    Expected<int> get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, const std::vector<EmbeddingViewWrapper> &input_embeddings,
        const std::vector<BufferPtr> &standalone_frame_embeddings, const std::vector<BufferPtr> &video_embeddings,
        const LLMGeneratorParams &params,
        const std::vector<std::unordered_map<std::string, BufferPtr>> &standalone_frame_deepstack_embeddings = {},
        const std::vector<std::unordered_map<std::string, BufferPtr>> &video_deepstack_embeddings = {});

    hailo_status process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs, std::map<std::string, MemoryView> &prefill_outputs,
        const std::vector<EmbeddingViewWrapper> &input_embeddings, EmbeddingsVectorState &standalone_frame_embeddings_state,
        EmbeddingsVectorState &video_embeddings_state);

    // Helper to compute embedding counts from buffer sizes
    std::vector<size_t> get_embeddings_count_per_item(const std::vector<BufferPtr> &embeddings_buffers) const;

    // Helpers for handle_vlm_generate_request - handles the two paths: raw embeddings vs raw frames to encode
    hailo_status handle_raw_embeddings_generation(uint32_t number_of_standalone_frames,
        const std::vector<uint32_t> &raw_video_embeddings_count_per_video);
    hailo_status handle_raw_frames_encoding(uint32_t number_of_standalone_frames, uint32_t number_of_video_frames,
        const std::vector<uint32_t> &raw_video_frames_count_per_video);

    hailo_status encode_standalone_frames(uint32_t number_of_standalone_frames, size_t encoder_output_frame_size);
    hailo_status encode_video_frames(uint32_t number_of_video_frames,
        const std::vector<uint32_t> &raw_video_frames_count_per_video, size_t encoder_output_frame_size);

    Expected<std::unordered_map<std::string, BufferPtr>> allocate_deepstack_buffers_for_frame();
    std::map<std::string, MemoryView> build_encoder_outputs_with_deepstack(
        BufferPtr &main_output, std::unordered_map<std::string, BufferPtr> &deepstack_buffers);

    struct DeepstackLayersInfo {
        std::string encoder_output_name;
        size_t frame_size;
        size_t tokens_per_frame;
        std::string decoder_input_suffix;
    };

    hailo_status build_deepstack_outputs_info();

    std::unique_ptr<InferenceManager> m_inference_manager_frame_encoder;

    int m_image_pad_token_id;
    int m_video_pad_token_id;
    hailo_3d_image_shape_t m_encoder_input_shape;  // Needed for VLMPreProcess creation

    bool m_support_raw_embeddings; // Indicates the model supports getting raw-embeddings for images (instead of getting them form the vision-encoder)

    std::vector<BufferPtr> m_current_standalone_frames_embeddings;
    std::vector<BufferPtr> m_current_videos_embeddings;
    std::vector<size_t> m_embeddings_count_per_video; // Per-video embedding counts (one entry per video)
    std::string m_encoder_embeddings_output_suffix; // From config JSON, used to resolve the full output name
    std::string m_encoder_embeddings_output_name;   // Full resolved output name

    // Encoder-output-suffix → decoder-input-suffix mapping, parsed from config JSON
    std::unordered_map<std::string, std::string> m_deepstack_encoder_to_decoder_suffix_map;

    // Per-frame deepstack encoder outputs: each entry maps deepstack decoder-input suffix to its buffer
    // TODO: HRT-20201 - Try to refactor this and the `m_current_standalone_frames_embeddings`, `m_current_videos_embeddings`
    std::vector<std::unordered_map<std::string, BufferPtr>> m_current_standalone_frame_deepstack_embeddings;
    std::vector<std::unordered_map<std::string, BufferPtr>> m_current_video_deepstack_embeddings;
    bool m_has_deepstack_layers;
    std::vector<DeepstackLayersInfo> m_deepstack_layers_info;
};

class VLMServerManager : public LLMServerManager
{
public:
    static Expected<std::unique_ptr<LLMServerManager>> create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager);

    VLMServerManager(std::shared_ptr<Session> session, std::unique_ptr<LLMServer> &&server);
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_VLM_SERVER_HPP_ */
