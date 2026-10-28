/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process_config.hpp
 * @brief Parsing of the pre-process section of a model configuration
 **/

#ifndef _HAILO_GENAI_LLM_PRE_PROCESS_CONFIG_HPP_
#define _HAILO_GENAI_LLM_PRE_PROCESS_CONFIG_HPP_

#include "pre_process.hpp"

#include <nlohmann/json.hpp>

namespace hailort
{
namespace genai
{

struct PreProcessConfig {
    PreProcessParams params;
    InputLayersNamesSuffixes suffixes;
    EmbeddingTables embedding_tables;
};

// How 'mrope_section' is read. TextOnly clears the flat one (a single section spanning each theta half) and keeps a
// declared group's as given; Multimodal requires the flat one, or every declared group's, to be non-empty.
enum class RopeLayout {
    TextOnly,
    Multimodal,
};

Expected<PreProcessConfig> parse_pre_process_config(const nlohmann::json &hailo_config_json, RopeLayout rope_layout);

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_LLM_PRE_PROCESS_CONFIG_HPP_ */
