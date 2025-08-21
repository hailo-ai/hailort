/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_tokenizer.hpp
 * @brief Hailo Tokenizer component
 **/

#ifndef _HAILO_HAILO_GENAI_TOKENIZER_HPP_
#define _HAILO_HAILO_GENAI_TOKENIZER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"

#include "tokenizers_cpp.h"

namespace hailort
{
namespace genai
{

class HailoTokenizer
{
public:
    static Expected<std::unique_ptr<HailoTokenizer>> create(const std::string &tokenizer_json_blob);

    HailoTokenizer(HailoTokenizer &&) = default;
    HailoTokenizer(const HailoTokenizer &) = delete;
    HailoTokenizer &operator=(HailoTokenizer &&) = delete;
    HailoTokenizer &operator=(const HailoTokenizer &) = delete;
    virtual ~HailoTokenizer() = default;

    Expected<std::vector<int>> text_to_tokens(const std::string &text);
    // When forcing printable strings, the tokenizer will buffer tokens that decodes into replacement-char () for future decoding
    Expected<std::string> tokens_to_text(const std::vector<int> &tokens, bool force_printable = false);

    void clear_buffered_tokens();

    HailoTokenizer(std::unique_ptr<tokenizers::Tokenizer> &&hf_tokenizer);
private:
    std::unique_ptr<tokenizers::Tokenizer> m_hf_tokenizer;

    std::vector<int> m_buffered_tokens;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_TOKENIZER_HPP_ */
