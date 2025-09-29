/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file constants.hpp
 * @brief Common constants for GenAI components
 **/

#ifndef _HAILO_HAILO_GENAI_CONSTANTS_HPP_
#define _HAILO_HAILO_GENAI_CONSTANTS_HPP_

#include <string>

namespace hailort
{
namespace genai
{

// Common resource names for GenAI components
static const std::string INPUT_EMB_BINARY = "embeddings.bin";
static const std::string TOKENIZER = "tokenizer.json";
static const std::string THETA = "rope_theta_data.bin";
static const std::string HAILO_CONFIG_JSON = "hailo-config.json";

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_CONSTANTS_HPP_ */
