/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
        std::shared_ptr<Event> frame_encoder_created_event);
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
        const std::vector<BufferPtr> &standalone_frame_embeddings, const std::vector<BufferPtr> &video_embeddings, const LLMGeneratorParams &params);

    hailo_status process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs, std::map<std::string, MemoryView> &prefill_outputs,
        const std::vector<EmbeddingViewWrapper> &input_embeddings, EmbeddingsVectorState &standalone_frame_embeddings_state,
        EmbeddingsVectorState &video_embeddings_state);

    std::unique_ptr<InferenceManager> m_inference_manager_frame_encoder;

    int m_image_pad_token_id;
    int m_video_pad_token_id;
    hailo_3d_image_shape_t m_encoder_input_shape;  // Needed for VLMPreProcess creation

    std::vector<BufferPtr> m_current_standalone_frames_embeddings;
    std::vector<BufferPtr> m_current_videos_embeddings;
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
