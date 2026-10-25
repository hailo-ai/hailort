/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file reasoning.hpp
 * @brief Reasoning span tracking for LLM generation
 **/

#ifndef _HAILO_GENAI_LLM_REASONING_HPP_
#define _HAILO_GENAI_LLM_REASONING_HPP_

#include "hailo/genai/llm/llm.hpp"

#include "pre_process.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

namespace hailort
{
namespace genai
{

// A thought can only be dropped from the context if it has both a start and an end token
bool is_reasoning_configured(int start_token_id, int end_token_id);

// The index of the prompt's last reasoning start, unless a reasoning end follows it or reasoning is not configured
std::optional<size_t> find_unclosed_reasoning_start(const std::vector<int> &tokens, int start_token_id,
    int end_token_id);

// Whether a token belongs to a reasoning span, its start and end included, given the span state before it
bool is_reasoning_span_token(int token, bool is_inside_reasoning, int start_token_id);

// Whether a token lies outside every reasoning span. Also moves the span state past it: a start enters a span and an
// end leaves it.
bool is_outside_reasoning_and_advance_span(int token, int start_token_id, int end_token_id, bool &is_inside_reasoning);

// Whether a generated token opens a tool call that keeps the generation's thoughts. A prompt-opened thought is
// never kept. is_inside_reasoning is the span state before the token.
bool is_tool_call_keeping_thoughts(int token, int tool_call_start_token_id, bool is_inside_reasoning,
    bool is_thought_opened_by_prompt);

// The tokens of a stop sequence ending the generation that the context holds nowhere else: the ones inside the thought
// it leaves open, and its last one, never a reasoning start or end. is_inside_reasoning is the state before it.
std::vector<int> stop_sequence_tokens_kept_in_context(const std::vector<int> &stop_sequence_tokens,
    bool is_inside_reasoning, int start_token_id, int end_token_id);

enum class DeviceCacheRewind {
    None,
    OffsetOnly,
    RestoreThenOffset,
    MissingSnapshot,
};

struct DeviceCacheRewindPlan {
    DeviceCacheRewind action;
    // Set when the rollback exceeds a group's ring, i.e. for RestoreThenOffset and MissingSnapshot
    std::optional<CacheRollbackLimit> exceeded_limit;
};

// How to drop a thought of rollback_tokens_count tokens from the device cache. Rewinding the offset suffices unless a
// ring cannot absorb the rollback, which needs the snapshot taken before the ring wrapped.
DeviceCacheRewindPlan plan_device_cache_rewind(const std::vector<AttentionGroupParams> &attention_groups,
    uint32_t prefill_input_tokens_count, size_t cache_usage_before_reasoning, size_t rollback_tokens_count,
    bool has_device_cache_snapshot);

struct RecoveryTokenDelivery {
    int token;
    LLMGeneratorCompletion::Status status;
    std::queue<int> remaining_recovery_tokens;
};

// The next recovery token to deliver. Only the last one carries termination_status, the others GENERATING. With no
// recovery token left, delivers INVALID_TOKEN_VALUE with termination_status.
RecoveryTokenDelivery deliver_next_recovery_token(std::queue<int> recovery_tokens,
    LLMGeneratorCompletion::Status termination_status);

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_REASONING_HPP_ */
