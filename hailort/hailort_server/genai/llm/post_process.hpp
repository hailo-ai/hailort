/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file post_process.hpp
 * @brief Implementation for LLM post processing
 **/

#ifndef _HAILO_HAILO_GENAI_LLM_POST_PROCESS_HPP_
#define _HAILO_HAILO_GENAI_LLM_POST_PROCESS_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"

#include "hailo/genai/llm/llm.hpp"

#include "eigen.hpp"
#include <vector>
#include <unordered_set>
#include <random>

namespace hailort
{
namespace genai
{


class LLMPostProcess
{
public:

    LLMPostProcess() :
        m_seed(HAILO_RANDOM_SEED), m_random_generator()
    {};

    using token_t = int;
    using scores_t = Eigen::VectorXf;
    using indices_t = Eigen::VectorXi;
    using indices_vector_t = std::vector<int>;

    token_t get_next_token(MemoryView next_token_scores,
        const std::unordered_set<int> &context_tokens_history, const LLMGeneratorParams &params);

    void reset_random_generator();
    void set_seed(uint32_t seed);

private:
    static void apply_repetition_penalty(scores_t &scores,
        const std::unordered_set<token_t> &context_tokens_history, float frequency_penalty);
    static token_t get_maximum_probability_token(const scores_t &scores);
    static std::pair<indices_t, scores_t> get_top_k_scores(const scores_t &scores, uint32_t top_k);
    static std::pair<indices_vector_t, scores_t> filter_by_top_p(const indices_t &top_k_indices,
        const scores_t &top_k_scores, const LLMGeneratorParams &params);
    token_t sample_from_probabilities(const indices_vector_t &filtered_indices, const scores_t &filtered_scores);

    uint32_t m_seed;
    std::mt19937 m_random_generator;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_LLM_POST_PROCESS_HPP_ */
