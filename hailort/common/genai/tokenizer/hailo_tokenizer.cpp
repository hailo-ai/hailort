/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_tokenizer.cpp
 * @brief Hailo tokenizer implementation
 **/

#include "hailo_tokenizer.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<HailoTokenizer>> HailoTokenizer::create(const std::string &tokenizer_json_blob)
{
    CHECK(!tokenizer_json_blob.empty(), HAILO_INVALID_ARGUMENT, "Creating Hailo Tokenizer failed, `tokenizer_json_blob` is empty");

    auto tokenizer = tokenizers::Tokenizer::FromBlobJSON(tokenizer_json_blob);
    CHECK_NOT_NULL_AS_EXPECTED(tokenizer, HAILO_OUT_OF_HOST_MEMORY);

    auto ptr = std::make_unique<HailoTokenizer>(std::move(tokenizer));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return ptr;
}

HailoTokenizer::HailoTokenizer(std::unique_ptr<tokenizers::Tokenizer> &&hf_tokenizer)
    : m_hf_tokenizer(std::move(hf_tokenizer))
{}

Expected<std::vector<int>> HailoTokenizer::text_to_tokens(const std::string &text)
{
    return m_hf_tokenizer->Encode(text);
}

Expected<std::string> HailoTokenizer::tokens_to_text(const std::vector<int> &tokens, bool force_printable)
{
    if (!force_printable) {
        return m_hf_tokenizer->Decode(tokens);
    }

    // If the result is not printable (contains replacement char), we will buffer the tokens and try to decode them later
    // This is a workaround for the tokenizer that sometimes generates un-printable strings when decodeing token-by-token
    m_buffered_tokens.insert(m_buffered_tokens.end(), tokens.begin(), tokens.end());
    auto result = m_hf_tokenizer->Decode(m_buffered_tokens);
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
