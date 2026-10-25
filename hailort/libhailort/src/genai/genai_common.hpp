/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file common_internal.hpp
 * @brief TODO: fill me
 **/

#ifndef _HAILO_COMMON_INTERNAL_HPP_
#define _HAILO_COMMON_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/genai/common.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/buffer.hpp"
#include "common/genai/config/embedding_tables.hpp"
#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
#include "common/genai/tokenizer/token_embedder.hpp"
#endif // HAILO_CLIENT_TOKENIZER_ENABLED

#include <map>
#include <string>
#include <unordered_set>

namespace hailort
{
namespace genai
{

// Forward declarations
class SessionWrapper;
class PromptTemplateHandler;

class GenAICommon
{
public:
    GenAICommon() = delete;

    static Expected<std::shared_ptr<SessionWrapper>> create_session_wrapper(
        std::shared_ptr<VDevice> vdevice, uint16_t connection_port);
    static hailo_status validate_kept_resources_found(const std::string &hef_path,
        const std::unordered_set<std::string> &kept_resource_names,
        const std::map<std::string, BufferPtr> &local_resources_buffers);
    // The server may clear its conversation on a failed load, so the template follows its reported usage
    static void resync_prompt_template_after_failed_load(hailo_status load_status,
        const Expected<size_t> &context_usage, PromptTemplateHandler &prompt_template_handler);
    static void resync_prompt_template(const Expected<size_t> &context_usage,
        PromptTemplateHandler &prompt_template_handler);
    static Expected<size_t> get_context_usage_size(SessionWrapper &session);

#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
    static Expected<EmbeddingTables> read_hef_embedding_tables(const std::string &hef_path);
    // The resources a host that tokenizes keeps instead of streaming; the server reads none of them from such a host
    static std::unordered_set<std::string> host_kept_resources(const EmbeddingTables &embedding_tables);
    static Expected<std::unique_ptr<TokenEmbedder<uint16_t>>> create_kept_table_embedder(
        const std::map<std::string, BufferPtr> &kept_resources, const EmbeddingTableParams &table, uint32_t features,
        const std::string &table_description, int image_pad_token_id = INVALID_TOKEN_VALUE,
        int video_pad_token_id = INVALID_TOKEN_VALUE);
#endif // HAILO_CLIENT_TOKENIZER_ENABLED

private:
    static hailo_status validate_genai_vdevice_params(const hailo_vdevice_params_t &vdevice_params);
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_COMMON_INTERNAL_HPP_ */
