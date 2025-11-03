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
    static constexpr uint32_t MAX_FRAMES_IN_SINGLE_GENERATION = 5; // TODO (HRT-17263): Decide about value, consider exporting to user

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

private:
    hailo_status parse_config_json(const MemoryView &config_json) override;

    // Override prefill phase to handle frame embeddings
    Expected<std::pair<int, LLMGeneratorCompletion::Status>> handle_prefill_phase(const std::vector<int> &tokens,
        const std::vector<MemoryView> &embeddings) override;

    // This function is used to process the prefill inputs and outputs, without handling (exporting) the generated token
    Expected<int> get_next_token_prefill(std::map<std::string, MemoryView> &prefill_inputs,
        std::map<std::string, MemoryView> &prefill_outputs, const std::vector<MemoryView> &input_embeddings,
        const std::vector<MemoryView> &frame_embeddings, const LLMGeneratorParams &params);

    hailo_status process_prefill_inputs_chunk(std::map<std::string, MemoryView> &prefill_inputs, std::map<std::string, MemoryView> &prefill_outputs,
        const std::vector<MemoryView> &input_embeddings, const std::vector<MemoryView> &frame_embeddings,
        uint32_t &current_frame_index, uint32_t &current_emb_index_in_frame);

    std::unique_ptr<InferenceManager> m_inference_manager_frame_encoder;
    std::vector<BufferPtr> m_frame_encoder_input_buffers;
    std::vector<BufferPtr> m_frame_encoder_output_buffers;

    int m_image_pad_token_id;

    std::vector<MemoryView> m_current_frame_embeddings;
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
