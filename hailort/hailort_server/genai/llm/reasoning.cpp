/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file reasoning.cpp
 * @brief Reasoning span tracking for LLM generation
 **/

#include "reasoning.hpp"
#include "common/genai/tokenizer/token_embedder.hpp"

#include <algorithm>
#include <iterator>

namespace hailort
{
namespace genai
{

bool is_reasoning_configured(int start_token_id, int end_token_id)
{
    return (INVALID_TOKEN_VALUE != start_token_id) && (INVALID_TOKEN_VALUE != end_token_id);
}

std::optional<size_t> find_unclosed_reasoning_start(const std::vector<int> &tokens, int start_token_id,
    int end_token_id)
{
    if (!is_reasoning_configured(start_token_id, end_token_id)) {
        return std::nullopt;
    }

    const auto last_start_iter = std::find(tokens.rbegin(), tokens.rend(), start_token_id);
    if (tokens.rend() == last_start_iter) {
        return std::nullopt;
    }
    const auto is_closed = (last_start_iter != std::find(tokens.rbegin(), last_start_iter, end_token_id));
    if (is_closed) {
        return std::nullopt;
    }

    return static_cast<size_t>(std::distance(tokens.begin(), last_start_iter.base()) - 1);
}

bool is_reasoning_span_token(int token, bool is_inside_reasoning, int start_token_id)
{
    return is_inside_reasoning || (start_token_id == token);
}

bool is_outside_reasoning_and_advance_span(int token, int start_token_id, int end_token_id, bool &is_inside_reasoning)
{
    const auto is_outside_reasoning = !is_reasoning_span_token(token, is_inside_reasoning, start_token_id);
    is_inside_reasoning = is_inside_reasoning ? (end_token_id != token) : (start_token_id == token);
    return is_outside_reasoning;
}

bool is_tool_call_keeping_thoughts(int token, int tool_call_start_token_id, bool is_inside_reasoning,
    bool is_thought_opened_by_prompt)
{
    const auto is_tool_call_start = (INVALID_TOKEN_VALUE != tool_call_start_token_id) &&
        (tool_call_start_token_id == token);
    return is_tool_call_start && !is_inside_reasoning && !is_thought_opened_by_prompt;
}

std::vector<int> stop_sequence_tokens_kept_in_context(const std::vector<int> &stop_sequence_tokens,
    bool is_inside_reasoning, int start_token_id, int end_token_id)
{
    std::vector<int> kept_tokens;
    for (size_t token_index = 0; token_index < stop_sequence_tokens.size(); token_index++) {
        const auto token = stop_sequence_tokens[token_index];
        const auto was_inside_reasoning = is_inside_reasoning;
        const auto is_outside_reasoning = is_outside_reasoning_and_advance_span(token, start_token_id, end_token_id,
            is_inside_reasoning);

        const auto has_closed_thought = was_inside_reasoning && !is_inside_reasoning;
        if (has_closed_thought) {
            kept_tokens.clear();
            continue;
        }
        const auto is_inside_open_thought = was_inside_reasoning && (start_token_id != token);
        const auto is_last_token = ((token_index + 1) == stop_sequence_tokens.size());
        const auto is_last_answer_token = is_outside_reasoning && is_last_token && (end_token_id != token);
        if (is_inside_open_thought || is_last_answer_token) {
            kept_tokens.push_back(token);
        }
    }
    return kept_tokens;
}

DeviceCacheRewindPlan plan_device_cache_rewind(const std::vector<AttentionGroupParams> &attention_groups,
    uint32_t prefill_input_tokens_count, size_t cache_usage_before_reasoning, size_t rollback_tokens_count,
    bool has_device_cache_snapshot)
{
    if (0 == rollback_tokens_count) {
        return {DeviceCacheRewind::None, std::nullopt};
    }

    const auto exceeded_limit = find_exceeded_cache_rollback_limit(attention_groups, prefill_input_tokens_count,
        cache_usage_before_reasoning, rollback_tokens_count);
    if (!exceeded_limit) {
        return {DeviceCacheRewind::OffsetOnly, std::nullopt};
    }
    if (!has_device_cache_snapshot) {
        return {DeviceCacheRewind::MissingSnapshot, exceeded_limit};
    }
    return {DeviceCacheRewind::RestoreThenOffset, exceeded_limit};
}

RecoveryTokenDelivery deliver_next_recovery_token(std::queue<int> recovery_tokens,
    LLMGeneratorCompletion::Status termination_status)
{
    if (recovery_tokens.empty()) {
        return {INVALID_TOKEN_VALUE, termination_status, std::move(recovery_tokens)};
    }

    const auto token = recovery_tokens.front();
    recovery_tokens.pop();
    const auto status = recovery_tokens.empty() ? termination_status : LLMGeneratorCompletion::Status::GENERATING;
    return {token, status, std::move(recovery_tokens)};
}

} /* namespace genai */
} /* namespace hailort */
