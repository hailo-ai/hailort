/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file post_process.cpp
 * @brief Speech2Text post-process
 **/

#include "post_process.hpp"
#include "utils.hpp"


namespace hailort
{

namespace genai
{

constexpr float32_t NEGATIVE_INFINITY = -std::numeric_limits<float32_t>::infinity();


Speech2TextPostProcess::Speech2TextPostProcess(size_t chunk_size_sec,
    token_t timestamp_begin_token_id, token_t eot_token_id, std::vector<token_t> &&blank_token_ids,
    std::vector<token_t> &&suppress_tokens_ids, float32_t time_precision, token_t max_initial_timestamp)
    : m_chunk_size_sec(chunk_size_sec),
    m_timestamp_begin_token_id(timestamp_begin_token_id),
    m_eot_token_id(eot_token_id), 
    m_blank_token_ids(std::move(blank_token_ids)),
    m_suppress_tokens_ids(std::move(suppress_tokens_ids)),
    m_time_precision(time_precision),
    m_max_initial_timestamp(max_initial_timestamp)
{}

std::pair<token_t, float32_t> Speech2TextPostProcess::get_next_token(Eigen::Map<Eigen::VectorXf> next_token_scores,
    const std::vector<token_t> &generated_tokens)
{
    Eigen::Map<const Eigen::VectorXi> generated_tokens_eigen(generated_tokens.data(), generated_tokens.size());

    suppress_tokens(next_token_scores, generated_tokens_eigen);
    apply_timestamps_rules(next_token_scores, generated_tokens_eigen);
    
    token_t next_token = argmax(next_token_scores);
    Eigen::VectorXf logprobs = log_softmax(next_token_scores);
    // Log-prob for the chosen token (only valid if not after EOS)
    float32_t logprob = (next_token == m_eot_token_id) ? 0.0f : logprobs(next_token);

    return std::make_pair(next_token, logprob);
}

void Speech2TextPostProcess::suppress_tokens(Eigen::Map<Eigen::VectorXf> &next_token_scores, const Eigen::Map<const Eigen::VectorXi> &generated_tokens)
{
    // Suppressing blank tokens - So the start of the transcription is not blank
    if (static_cast<size_t>(generated_tokens.size()) == CONTEXT_TOKENS_COUNT) {
        for (int token_id : m_blank_token_ids) {
            next_token_scores(token_id) = NEGATIVE_INFINITY;
        }
    }

    // Suppressing special tokens
    for (int token_id : m_suppress_tokens_ids) {
        next_token_scores(token_id) = NEGATIVE_INFINITY;
    }
}

void Speech2TextPostProcess::apply_timestamps_rules(Eigen::Map<Eigen::VectorXf> &next_token_scores, const Eigen::Map<const Eigen::VectorXi> &generated_tokens)
{
    int vocab_size = static_cast<int>(next_token_scores.size());
    Eigen::VectorXi sampled_tokens = generated_tokens.segment(CONTEXT_TOKENS_COUNT, static_cast<size_t>(generated_tokens.size()) - CONTEXT_TOKENS_COUNT);

    // Timestamps always appear in pairs, except directly before EOT;
    // Checking if last token and penultimate token were timestamps
    bool last_was_timestamp = (sampled_tokens.size() >= 1 && sampled_tokens(sampled_tokens.size()-1) >= m_timestamp_begin_token_id);
    bool penultimate_was_timestamp = (sampled_tokens.size() < 2 || sampled_tokens(sampled_tokens.size()-2) >= m_timestamp_begin_token_id);

    // Mask logic: forbid tokens based on timestamp rules
    if (last_was_timestamp) {
        if (penultimate_was_timestamp) {
            // Timestamps always appear in pairs - if both last and penultimate tokens are timestamps,
            // then the next token has to be non-timestamp
            next_token_scores.segment(m_timestamp_begin_token_id, vocab_size - m_timestamp_begin_token_id).setConstant(NEGATIVE_INFINITY);
        } else {
            // if only last token was a timestamp, then the next token cannot be normal text token
            next_token_scores.segment(0, m_eot_token_id).setConstant(NEGATIVE_INFINITY);
        }
    }

    std::vector<token_t> generated_timestamps;
    generated_timestamps.reserve(sampled_tokens.size());
    for (int i = 0; i < sampled_tokens.size(); ++i) {
        if (sampled_tokens(i) >= m_timestamp_begin_token_id) {
            generated_timestamps.push_back(sampled_tokens(i));
        }
    }
    if (!generated_timestamps.empty()) {
        // Timestamps shouldn't decrease; forbid timestamp tokens smaller than the last
        token_t timestamp_last = (last_was_timestamp && !penultimate_was_timestamp)
            ? generated_timestamps.back()
            : generated_timestamps.back() + 1;
        int len = timestamp_last - m_timestamp_begin_token_id;
        if (len > 0) {
            next_token_scores.segment(m_timestamp_begin_token_id, len).setConstant(NEGATIVE_INFINITY);
        }
    }

    if (static_cast<size_t>(generated_tokens.size()) == CONTEXT_TOKENS_COUNT) {
        // Suppress generating non-timestamp tokens at the beginning
        next_token_scores.segment(0, m_timestamp_begin_token_id).setConstant(NEGATIVE_INFINITY);

        if (m_max_initial_timestamp > 0) {
            int last_allowed = m_timestamp_begin_token_id + m_max_initial_timestamp;
            int start = std::max(last_allowed + 1, 0);
            if (start < vocab_size) {
                next_token_scores.segment(start, vocab_size - start).setConstant(NEGATIVE_INFINITY);
            }
        }
    }

    // Ignore timestamps after chunk size
    // For example, if chunk_size is 10 seconds, then we want to ignore timestamps tokens that represent times larger than 10 seconds
    token_t ignore_timestamps_from = static_cast<token_t>(static_cast<float32_t>(m_chunk_size_sec) / m_time_precision) + m_timestamp_begin_token_id;
    if (ignore_timestamps_from < vocab_size) {
        next_token_scores.segment(ignore_timestamps_from, vocab_size - ignore_timestamps_from).setConstant(NEGATIVE_INFINITY);
    }

    // If sum of probability over timestamps is above any other token, sample timestamp
    Eigen::RowVectorXf logprobs = log_softmax(next_token_scores);
    float32_t timestamp_logprob = logsumexp(logprobs.segment(m_timestamp_begin_token_id, vocab_size - m_timestamp_begin_token_id));
    float32_t max_text_token_logprob = logprobs.segment(0, m_timestamp_begin_token_id).maxCoeff();
    if (timestamp_logprob > max_text_token_logprob) {
        next_token_scores.segment(0, m_timestamp_begin_token_id).setConstant(NEGATIVE_INFINITY);
    }
}


} /* namespace genai */

} /* namespace hailort */
