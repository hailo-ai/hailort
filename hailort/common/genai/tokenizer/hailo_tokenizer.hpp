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

#include "tokenizers_c.h"
#include <vector>

namespace hailort
{
namespace genai
{

class HailoTokenizer
{
public:
    static Expected<std::unique_ptr<HailoTokenizer>> create(const MemoryView tokenizer_json);

    HailoTokenizer(HailoTokenizer &&) = default;
    HailoTokenizer(const HailoTokenizer &) = delete;
    HailoTokenizer &operator=(HailoTokenizer &&) = delete;
    HailoTokenizer &operator=(const HailoTokenizer &) = delete;
    virtual ~HailoTokenizer();

    Expected<std::vector<int>> text_to_tokens(const std::string &text, bool add_special_tokens = false);
    // When forcing printable strings, the tokenizer will buffer tokens that decodes into replacement-char () for future decoding
    Expected<std::string> tokens_to_text(const std::vector<int> &tokens, bool force_printable = false, bool skip_special_tokens = false);

    void clear_buffered_tokens();

    HailoTokenizer(TokenizerHandle tokenizer_handle);
private:
    Expected<std::string> decode_internal(const std::vector<int> &tokens, bool skip_special_tokens = false);

    TokenizerHandle m_tokenizer_handle;
    std::vector<int> m_buffered_tokens;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_TOKENIZER_HPP_ */
