/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_common.cpp
 * @brief Common functions for GenAI
 **/

#include "genai_common.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
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

#ifdef HAILO_CLIENT_TOKENIZER_ENABLED
Expected<EmbeddingTables> GenAICommon::read_hef_embedding_tables(const std::string &hef_path)
{
    const std::unordered_set<std::string> CONFIG_ONLY = {HAILO_CONFIG_JSON};
    TRY(const auto parse_result, Hef::parse_hef_for_transfer(hef_path, CONFIG_ONLY));
    const auto config_resource = parse_result.local_resources_buffers.find(HAILO_CONFIG_JSON);
    CHECK(parse_result.local_resources_buffers.end() != config_resource, HAILO_NOT_FOUND,
        "The HEF '{}' carries no '{}' resource", hef_path, HAILO_CONFIG_JSON);

    return read_embedding_tables(config_resource->second->as_view());
}

std::unordered_set<std::string> GenAICommon::host_kept_resources(const EmbeddingTables &embedding_tables)
{
    std::unordered_set<std::string> kept_resources = {TOKENIZER, embedding_tables.token_embeddings.resource};
    if (embedding_tables.per_layer_inputs) {
        kept_resources.insert(embedding_tables.per_layer_inputs->resource);
    }

    return kept_resources;
}

Expected<std::unique_ptr<TokenEmbedder<uint16_t>>> GenAICommon::create_kept_table_embedder(
    const std::map<std::string, BufferPtr> &kept_resources, const std::string &resource, uint32_t features,
    const std::string &table_description, int image_pad_token_id, int video_pad_token_id)
{
    const auto kept_resource = kept_resources.find(resource);
    CHECK(kept_resources.end() != kept_resource, HAILO_NOT_AVAILABLE,
        "The {} table '{}' is not among the HEF's resources", table_description, resource);
    const auto &table = kept_resource->second;
    TRY(const auto rows_count, embedding_table_rows_count(table->size(), features, resource));

    return TokenEmbedder<uint16_t>::create(table, rows_count, features, image_pad_token_id, video_pad_token_id);
}
#endif // HAILO_CLIENT_TOKENIZER_ENABLED

} /* namespace genai */
} /* namespace hailort */
