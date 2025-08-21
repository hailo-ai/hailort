/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file post_process.cpp
 * @brief Implementation for LLM post processing
 **/

#include "post_process.hpp"

#include <queue>

namespace hailort
{
namespace genai
{

constexpr float32_t FLOAT_IGNORE_OPERATION = 1.0f;

void LLMPostProcess::apply_repetition_penalty(Eigen::VectorXf &scores,
    const std::unordered_set<int> &context_tokens_history, float32_t frequency_penalty)
{
    for (int token : context_tokens_history) {
        assert(token >= 0 && token < scores.size());
        scores[token] = scores[token] > 0 ? scores[token] / frequency_penalty : scores[token] * frequency_penalty;
    }
}

int LLMPostProcess::get_maximum_probability_token(const Eigen::VectorXf &scores)
{
    size_t max_idx;
    scores.maxCoeff(&max_idx);
    return static_cast<int>(max_idx);
}

/*
Leaving this dead-code here. once benchmarking with real data, we'll decide weather to go for the iterative impl (min-heap) or the recursive one.

void quickselect(std::vector<int> &indices, const Eigen::VectorXf &scores, int left, int right, int k) {
    if (left >= right) return;
    float pivot = scores[indices[right]];
    int partition_index = left;

    for (int i = left; i < right; ++i) {
        if (scores[indices[i]] > pivot) {
            std::swap(indices[i], indices[partition_index]);
            ++partition_index;
        }
    }
    std::swap(indices[partition_index], indices[right]);

    if (k < partition_index) {
        quickselect(indices, scores, left, partition_index - 1, k);
    } else if (k > partition_index) {
        quickselect(indices, scores, partition_index + 1, right, k);
    }
}

std::pair<Eigen::VectorXi, Eigen::VectorXf> _get_top_k_scores(const Eigen::VectorXf &scores, uint32_t top_k)
{
    // Create an indices vector using std::iota
    std::vector<int> indices(scores.size());
    std::iota(indices.begin(), indices.end(), 0);

    // Quickselect to find top_k largest elements
    quickselect(indices, scores, 0, (int)(indices.size() - 1), top_k);

    // Extract top_k indices and sort them by scores (ascending)
    std::vector<int> top_k_indices(indices.begin(), indices.begin() + top_k);
    std::sort(top_k_indices.begin(), top_k_indices.end(),
              [&scores](int a, int b) { return scores[a] < scores[b]; });

    // Map the sorted indices to Eigen structures
    Eigen::VectorXi top_k_indices_eigen = Eigen::Map<Eigen::VectorXi>(top_k_indices.data(), top_k);
    Eigen::VectorXf top_k_scores = scores(top_k_indices_eigen);

    return {top_k_indices_eigen, top_k_scores};
}
*/

std::pair<Eigen::VectorXi, Eigen::VectorXf> LLMPostProcess::get_top_k_scores(const Eigen::VectorXf& scores, uint32_t top_k)
{   
    using ScoreIndex = std::pair<float32_t, int>;
    std::priority_queue<ScoreIndex, 
                        std::vector<ScoreIndex>,
                        std::greater<ScoreIndex>> min_heap;

    // Initial fill of the heap
    for (uint32_t i = 0; i < top_k; i++) {
        min_heap.push({scores[i], i});
    }

    // Process remaining elements
    for (uint32_t i = top_k; i < (uint32_t)scores.size(); i++) {
        if (scores[i] > min_heap.top().first) {
            min_heap.pop();
            min_heap.push({scores[i], i});
        }
    }

    // Extract results in ascending order
    Eigen::VectorXi top_k_indices_eigen(top_k);
    Eigen::VectorXf top_k_scores(top_k);
    
    for (uint32_t i = 0; i < top_k; i++) {
        auto &pair = min_heap.top();
        auto &score = pair.first;
        auto &idx = pair.second;
        top_k_indices_eigen[i] = idx;
        top_k_scores[i] = score;
        min_heap.pop();
    }

    return {std::move(top_k_indices_eigen), std::move(top_k_scores)};
}

std::pair<std::vector<int>, Eigen::VectorXf> LLMPostProcess::filter_by_top_p(
    const Eigen::VectorXi &top_k_indices, const Eigen::VectorXf &top_k_scores, 
    const LLMGeneratorParams &params)
{
    // apply softmax to the scores
    // TODO (HRT-16225): Can be optimized by breaking into steps https://stackoverflow.com/questions/37658651/eigen-coding-styles-effect-on-performance
    Eigen::VectorXf normalized_scores = (top_k_scores.array() - top_k_scores.maxCoeff()).array().exp();
    normalized_scores /= normalized_scores.sum();

    // Cumulative sum for top-p filtering
    std::partial_sum(normalized_scores.data(), normalized_scores.data() + normalized_scores.size(), normalized_scores.data());

    // Filter indices based on top-p and top_p_min_val_to_keep
    std::vector<int> filtered_indices; // TODO (HRT-16225): Initialize in class ctor?
    std::vector<float32_t> filtered_scores; // TODO (HRT-16225): Initialize in class ctor?
    for (int i = 0; i < normalized_scores.size(); ++i) {
        if (normalized_scores[i] > (1.0f - params.top_p()) ||
            (i >= normalized_scores.size() - 1))
        {
            filtered_indices.push_back(top_k_indices[i]);
            filtered_scores.push_back(top_k_scores[i]); // Directly use top_k_scores without accessing next_token_scores
        } // TODO (HRT-16225): Can be optimized, as we can break the loop once we reach the top_p threshold and fill filterred_indices and filtered_scores with the remaining elements
    }

    // Normalize filtered scores
    // TODO: Can be optimized by breaking into steps https://stackoverflow.com/questions/37658651/eigen-coding-styles-effect-on-performance
    Eigen::VectorXf filtered_scores_eigen = Eigen::VectorXf::Map(filtered_scores.data(), filtered_scores.size());
    filtered_scores_eigen = (filtered_scores_eigen.array() - filtered_scores_eigen.maxCoeff()).array().exp();
    filtered_scores_eigen /= filtered_scores_eigen.sum();

    return {filtered_indices, filtered_scores_eigen};
}

int LLMPostProcess::sample_from_probabilities(const std::vector<int> &filtered_indices, const Eigen::VectorXf &filtered_scores)
{
    std::discrete_distribution<> dist(filtered_scores.data(), filtered_scores.data() + filtered_scores.size());
    return filtered_indices[dist(m_random_generator)];
}

int LLMPostProcess::get_next_token(MemoryView next_token_scores_view,
    const std::unordered_set<int> &context_tokens_history, const LLMGeneratorParams &params)
{
    // Try to make it nicer
    Eigen::VectorXf next_token_scores = Eigen::VectorXf::Map(reinterpret_cast<float32_t*>(next_token_scores_view.data()),
        (next_token_scores_view.size() / sizeof(float32_t)));

    // Apply temperature and repetition penalty
    if (FLOAT_IGNORE_OPERATION != params.temperature()) {
        // Cosdiering removing it if eigen can handle it
        next_token_scores /= params.temperature();
    }
    if (FLOAT_IGNORE_OPERATION != params.frequency_penalty()) {
        // Cosdiering removing it if eigen can handle it
        apply_repetition_penalty(next_token_scores, context_tokens_history, params.frequency_penalty());
    }

    if (!params.do_sample()) {
        return get_maximum_probability_token(next_token_scores);
    }

    auto top_k_pair = get_top_k_scores(next_token_scores, params.top_k());
    auto &top_k_indices = top_k_pair.first;
    auto &top_k_scores = top_k_pair.second;

    auto top_p_pair = filter_by_top_p(top_k_indices, top_k_scores, params);
    auto &filtered_indices = top_p_pair.first;
    auto &filtered_scores = top_p_pair.second;

    return sample_from_probabilities(filtered_indices, filtered_scores);
}

void LLMPostProcess::reset_random_generator()
{
    // Resetting the generation seed to its initial value
    m_random_generator.seed(m_seed);
}

void LLMPostProcess::set_seed(uint32_t seed)
{
    if (seed == HAILO_RANDOM_SEED) {
        seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    m_seed = seed;
    m_random_generator.seed(seed);
}

} /* namespace genai */
} /* namespace hailort */
