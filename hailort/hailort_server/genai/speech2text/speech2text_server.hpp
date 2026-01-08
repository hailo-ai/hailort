/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text_server.hpp
 * @brief Speech2Text server
 **/

#ifndef _HAILO_GENAI_SPEECH2TEXT_SERVER_HPP_
#define _HAILO_GENAI_SPEECH2TEXT_SERVER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/genai/speech2text/speech2text.hpp"
#include "common/utils.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"
#include "genai_server.hpp"
#include "hailo/vdevice.hpp"
#include "inference_manager.hpp"
#include "hailo_tokenizer.hpp"
#include "token_embedder.hpp"
#include "speech2text/post_process.hpp"

#include "hailort_server.hpp"
#include "utils.hpp"

namespace hailort
{
namespace genai
{

class Speech2TextServer
{
public:
    static constexpr Speech2TextTask DEFAULT_SPEECH2TEXT_TASK = Speech2TextTask::TRANSCRIBE;
    static constexpr std::string_view DEFAULT_SPEECH2TEXT_LANGUAGE = "";
    static constexpr float32_t DEFAULT_SPEECH2TEXT_REPETITION_PENALTY = Speech2TextPostProcess::NO_REPETITION_PENALTY_VALUE;

    static Expected<std::unique_ptr<Speech2TextServer>> create_unique(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager);

    Speech2TextServer(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager, Speech2TextGeneratorParams &&generator_params);
    Speech2TextServer(Speech2TextServer &&) = delete;
    Speech2TextServer(const Speech2TextServer &) = delete;
    Speech2TextServer &operator=(Speech2TextServer &&) = delete;
    Speech2TextServer &operator=(const Speech2TextServer &) = delete;
    virtual ~Speech2TextServer();

    Expected<Buffer> handle_create_speech2text_request(const MemoryView &request);
    Expected<Buffer> handle_generate_request(const MemoryView &request);
    Expected<Buffer> handle_tokenize_request(const MemoryView &request);

private:
    hailo_status parse_config_json(const nlohmann::json &hailo_config_json);
    hailo_status set_default_config_params();
    Expected<int> language_to_token_id(const std::string &language);
    Expected<int> task_to_token_id(Speech2TextTask task);
    Expected<std::vector<Speech2Text::SegmentInfo>> process_input_audio(const MemoryView &audio);
    Expected<std::pair<std::vector<int>, float32_t>> decoder_loop(const std::vector<int> &context_tokens);
    Eigen::Map<Eigen::VectorXf> get_next_token_scores(int next_token_idx);
    Expected<int> update_segments_and_calc_seek(const std::vector<int> &tokens, float32_t sum_logprobs, int seek, float32_t segment_duration_sec,
        uint32_t segment_size, float32_t time_offset, int input_stride, std::vector<Speech2Text::SegmentInfo> &segments_infos);
    Expected<int> detect_language();

    SessionWrapper m_session;
    std::shared_ptr<VDeviceManager> m_vdevice_manager;

    std::unique_ptr<InferenceManager> m_inference_manager_encoder;
    std::unique_ptr<InferenceManager> m_inference_manager_decoder;
    std::unique_ptr<HailoTokenizer> m_tokenizer;
    std::unique_ptr<TokenEmbedder<uint8_t>> m_token_embedder;
    Speech2TextPostProcess m_post_process;
    Speech2TextGeneratorParams m_generator_params;
    int m_decoder_seq_length;

    Buffer m_next_token_scores_buffer;
    std::vector<std::tuple<std::string, size_t, hailo_quant_info_t>> m_embeddings_outputs_info; // <output name, number of columns, quantization info>
    size_t m_vocab_size;
    size_t m_embeddings_outputs_row_size;

    // Buffers
    BufferPtr m_encoder_input_buffer;
    BufferPtr m_encoder_output_buffer;

    std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>> m_decoder_buffers;
    std::map<std::string, MemoryView> m_decoder_inputs;
    std::map<std::string, MemoryView> m_decoder_outputs;
    
    std::unordered_map<Speech2TextTask, int> m_task_to_token_id_map;

    int m_decoder_start_token_id;
    int m_task_transcribe_token_id;
    int m_task_translate_token_id;
    int m_timestamp_begin_token_id;
    int m_eos_token_id;
    int m_first_language_token_id;
    int m_last_language_token_id;
    int m_chunk_size_seconds;
    int m_num_mel_bins;
    int m_n_audio_ctx;
    int m_repetition_penalty_window_size;
    std::vector<int> m_special_tokens_ids_to_suppress;
    std::string m_encoder_model_name;
    std::string m_decoder_model_name;
    std::string m_decoder_input_name__common_encoder_decoder_layer;
    std::string m_decoder_input_name__input_tokens_layer;
    std::vector<std::string> m_decoder_ordered_outputs_names__embeddings;

    inline bool is_timestamp_token(int token) const
    {
        // Token larger than the first timestamp token is a timestamp token
        // TODO: Consider adding a check for the max timestamp token id
        return token >= m_timestamp_begin_token_id;
    }
};

class Speech2TextServerManager : public GenAIServerManager
{
public:
    static Expected<std::unique_ptr<Speech2TextServerManager>> create(std::shared_ptr<Session> session, std::shared_ptr<VDeviceManager> vdevice_manager);

    Speech2TextServerManager(std::shared_ptr<Session> session, std::unique_ptr<Speech2TextServer> &&server);

protected:
    std::unique_ptr<Speech2TextServer> m_server;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_SPEECH2TEXT_SERVER_HPP_ */
