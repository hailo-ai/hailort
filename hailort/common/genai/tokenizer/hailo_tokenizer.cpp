/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_tokenizer.cpp
 * @brief Hailo tokenizer implementation
 **/

#include "hailo_tokenizer.hpp"
#include "hailo/buffer.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<HailoTokenizer>> HailoTokenizer::create(const MemoryView tokenizer_json)
{
    CHECK(!tokenizer_json.empty(), HAILO_INVALID_ARGUMENT, "Creating Hailo Tokenizer failed, `tokenizer_json` is empty");

    auto tokenizer_handle = tokenizers_new_from_str(reinterpret_cast<const char*>(tokenizer_json.data()), tokenizer_json.size());
    CHECK_NOT_NULL_AS_EXPECTED(tokenizer_handle, HAILO_OUT_OF_HOST_MEMORY);

    auto ptr = std::make_unique<HailoTokenizer>(tokenizer_handle);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return ptr;
}

HailoTokenizer::HailoTokenizer(TokenizerHandle tokenizer_handle)
    : m_tokenizer_handle(tokenizer_handle)
{}

HailoTokenizer::~HailoTokenizer()
{
    if (m_tokenizer_handle != nullptr) {
        tokenizers_free(m_tokenizer_handle);
    }
}

Expected<std::vector<int>> HailoTokenizer::text_to_tokens(const std::string &text, bool add_special_tokens)
{
    TokenizerEncodeResult result;
    tokenizers_encode(m_tokenizer_handle, text.data(), text.length(), static_cast<int>(add_special_tokens), &result);
    std::vector<int> tokens(result.token_ids, result.token_ids + result.len);
    tokenizers_free_encode_results(&result, 1);
    return tokens;
}

Expected<std::string> HailoTokenizer::decode_internal(const std::vector<int> &tokens, bool skip_special_tokens)
{
    tokenizers_decode(m_tokenizer_handle, reinterpret_cast<const uint32_t*>(tokens.data()), tokens.size(), static_cast<int>(skip_special_tokens));

    // `tokenizers_get_decode_str` sets `data` to point to an internal buffer
    // owned by the tokenizer handle. The memory is managed by the handle and
    // freed automatically, so `data` must not be manually released.
    const char* data;
    size_t len;
    tokenizers_get_decode_str(m_tokenizer_handle, &data, &len);
    return std::string(data, len);
}


Expected<std::string> HailoTokenizer::tokens_to_text(const std::vector<int> &tokens, bool force_printable, bool skip_special_tokens)
{
    if (!force_printable) {
        return decode_internal(tokens, skip_special_tokens);
    }

    // If the result is not printable (contains replacement char), we will buffer the tokens and try to decode them later
    // This is a workaround for the tokenizer that sometimes generates un-printable strings when decodeing token-by-token
    m_buffered_tokens.insert(m_buffered_tokens.end(), tokens.begin(), tokens.end());
    TRY(auto result, decode_internal(m_buffered_tokens, skip_special_tokens));
    if (!StringUtils::contains_replacement_char(result)) {
        clear_buffered_tokens();
        return result;
    }
    return make_unexpected(HAILO_NOT_AVAILABLE);
}

void HailoTokenizer::clear_buffered_tokens()
{
    m_buffered_tokens.clear();
}

} /* namespace genai */
} /* namespace hailort */
