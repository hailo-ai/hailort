/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file post_process.hpp
 * @brief Speech2Text post-process
 **/

#ifndef _HAILO_GENAI_SPEECH2TEXT_POST_PROCESS_HPP_
#define _HAILO_GENAI_SPEECH2TEXT_POST_PROCESS_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"
#include "eigen.hpp"


namespace hailort
{
namespace genai
{

using token_t = int;
constexpr size_t CONTEXT_TOKENS_COUNT = 3; // <sot><language_id><task> - after these tokens, the sample actually begins

class Speech2TextPostProcess
{
public:
    Speech2TextPostProcess(size_t chunk_size_sec, token_t timestamp_begin_token_id, token_t eot_token_id, std::vector<token_t> &&blank_token_ids,
        std::vector<token_t> &&suppress_tokens_ids, float32_t time_precision, token_t max_initial_timestamp = 0);

    // next_token_scores: A tensor representing the raw output scores for each token in the vocabulary.
    // generated_tokens: A vector of token IDs representing the current sequence of tokens generated so far.
    // returns: A pair of the next token and its log probability
    std::pair<token_t, float32_t> get_next_token(Eigen::Map<Eigen::VectorXf> next_token_scores, const std::vector<token_t> &generated_tokens);

    Speech2TextPostProcess() = default;
    Speech2TextPostProcess(Speech2TextPostProcess &&) = delete;
    Speech2TextPostProcess(const Speech2TextPostProcess &) = delete;
    Speech2TextPostProcess &operator=(Speech2TextPostProcess &&) = default;
    Speech2TextPostProcess &operator=(const Speech2TextPostProcess &) = delete;
    virtual ~Speech2TextPostProcess() = default;

private:
    void suppress_tokens(Eigen::Map<Eigen::VectorXf> &next_token_scores, const Eigen::Map<const Eigen::VectorXi> &generated_tokens);
    void apply_timestamps_rules(Eigen::Map<Eigen::VectorXf> &next_token_scores, const Eigen::Map<const Eigen::VectorXi> &generated_tokens);

    size_t m_chunk_size_sec;
    token_t m_timestamp_begin_token_id;
    token_t m_eot_token_id;
    std::vector<token_t> m_blank_token_ids;
    std::vector<token_t> m_suppress_tokens_ids;
    float32_t m_time_precision; // The time precision of the timestamp tokens, usually 0.02

    // Highest allowed timestamp token index at the start, preventing Whisper from skipping too far ahead.
    token_t m_max_initial_timestamp;
};

} /* namespace genai */  
} /* namespace hailort */

#endif /* _HAILO_GENAI_SPEECH2TEXT_POST_PROCESS_HPP_ */
 