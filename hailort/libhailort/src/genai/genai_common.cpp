/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_common.cpp
 * @brief Common functions for GenAI
 **/

#include "genai_common.hpp"
#include "llm/minja_wrapper/minja_wrapper.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"
#include "common/genai/constants.hpp"
#include "common/logger_macros.hpp"
#include "hailo/hef.hpp"


namespace hailort
{
namespace genai
{

hailo_status GenAICommon::validate_genai_vdevice_params(const hailo_vdevice_params_t &vdevice_params)
{
    CHECK(vdevice_params.scheduling_algorithm != HAILO_SCHEDULING_ALGORITHM_NONE, HAILO_NOT_SUPPORTED,
        "Working without scheduler is not supported for GenAI");

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<SessionWrapper>> GenAICommon::create_session_wrapper(std::shared_ptr<VDevice> vdevice,
    uint16_t connection_port)
{
    CHECK_SUCCESS(validate_genai_vdevice_params(vdevice->get_params()));

    TRY(auto hailo_session, vdevice->create_session(connection_port));
    auto session_wrapper = make_shared_nothrow<SessionWrapper>(hailo_session);
    CHECK_NOT_NULL_AS_EXPECTED(session_wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return session_wrapper;
}

hailo_status GenAICommon::validate_kept_resources_found(const std::string &hef_path,
    const std::unordered_set<std::string> &kept_resource_names,
    const std::map<std::string, BufferPtr> &local_resources_buffers)
{
    for (const auto &resource : kept_resource_names) {
        CHECK(contains(local_resources_buffers, resource), HAILO_NOT_AVAILABLE,
            "The HEF '{}' carries no '{}' resource", hef_path, resource);
    }

    return HAILO_SUCCESS;
}

void GenAICommon::resync_prompt_template_after_failed_load(hailo_status load_status,
    const Expected<size_t> &context_usage, PromptTemplateHandler &prompt_template_handler)
{
    LOGGER__ERROR("Failed to load the context, status {}", load_status);
    resync_prompt_template(context_usage, prompt_template_handler);
}

Expected<size_t> GenAICommon::get_context_usage_size(SessionWrapper &session)
{
    TRY(auto request, LLMGetContextUsageSizeSerializer::serialize_request());
    TRY(auto reply, session.execute(MemoryView(request)));
    return LLMGetContextUsageSizeSerializer::deserialize_reply(MemoryView(*reply));
}

void GenAICommon::resync_prompt_template(const Expected<size_t> &context_usage,
    PromptTemplateHandler &prompt_template_handler)
{
    if (!context_usage) {
        LOGGER__ERROR("Failed to get the context usage to resync the prompt template, status {}",
            context_usage.status());
        return;
    }

    if (0 == *context_usage) {
        prompt_template_handler.reset_state();
    }
}

#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
Expected<EmbeddingTables> GenAICommon::read_hef_embedding_tables(const std::string &hef_path)
{
    const std::unordered_set<std::string> CONFIG_ONLY = {HAILO_CONFIG_JSON};
    TRY(const auto parse_result, Hef::parse_hef_for_transfer(hef_path, CONFIG_ONLY));
    const auto config_resource = parse_result.local_resources_buffers.find(HAILO_CONFIG_JSON);
    CHECK(parse_result.local_resources_buffers.end() != config_resource, HAILO_NOT_AVAILABLE,
        "The HEF '{}' carries no '{}' resource", hef_path, HAILO_CONFIG_JSON);

    return read_embedding_tables(config_resource->second->as_view());
}

std::unordered_set<std::string> GenAICommon::host_kept_resources(const EmbeddingTables &embedding_tables)
{
    std::unordered_set<std::string> kept_resources = {TOKENIZER};
    const auto keep_table = [&kept_resources](const EmbeddingTableParams &table) {
        kept_resources.insert(table.resource);
        if (table.codebook) {
            kept_resources.insert(table.codebook->resource);
        }
    };
    keep_table(embedding_tables.token_embeddings);
    if (embedding_tables.per_layer_inputs) {
        keep_table(*embedding_tables.per_layer_inputs);
    }

    return kept_resources;
}

static Expected<BufferPtr> find_kept_resource(const std::map<std::string, BufferPtr> &kept_resources,
    const std::string &resource, const std::string &resource_description)
{
    const auto kept_resource = kept_resources.find(resource);
    CHECK(kept_resources.end() != kept_resource, HAILO_NOT_AVAILABLE,
        "The {} is not among the HEF's resources", resource_description);

    return BufferPtr(kept_resource->second);
}

Expected<std::unique_ptr<TokenEmbedder<uint16_t>>> GenAICommon::create_kept_table_embedder(
    const std::map<std::string, BufferPtr> &kept_resources, const EmbeddingTableParams &table, uint32_t features,
    const std::string &table_description, int image_pad_token_id, int video_pad_token_id)
{
    TRY(auto table_buffer, find_kept_resource(kept_resources, table.resource,
        fmt::format("{} table '{}'", table_description, table.resource)));
    BufferPtr codebook_buffer;
    MemoryView codebook_view;
    if (table.codebook) {
        TRY(codebook_buffer, find_kept_resource(kept_resources, table.codebook->resource,
            fmt::format("codebook '{}' of the {} table '{}'", table.codebook->resource, table_description,
            table.resource)));
        codebook_view = codebook_buffer->as_view();
    }
    TRY(auto embedder, create_table_embedder(table, table_buffer->as_view(), codebook_view, features,
        image_pad_token_id, video_pad_token_id));
    embedder->add_resource_guard(std::move(table_buffer));
    if (codebook_buffer) {
        embedder->add_resource_guard(std::move(codebook_buffer));
    }

    return embedder;
}
#endif // HAILO_CLIENT_TOKENIZER_ENABLED

} /* namespace genai */
} /* namespace hailort */
