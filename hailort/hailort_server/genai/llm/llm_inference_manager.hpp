/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm_inference_manager.hpp
 * @brief Implementation for LLM inference manager
 **/

#ifndef _HAILO_HAILO_GENAI_LLM_INFERENCE_MANAGER_HPP_
#define _HAILO_HAILO_GENAI_LLM_INFERENCE_MANAGER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/infer_model.hpp"
#include "inference_manager.hpp"


namespace hailort
{
namespace genai
{


class LLMInferenceManager : public InferenceManager
{
public:
    static Expected<std::unique_ptr<InferenceManager>> create(std::shared_ptr<hailort::VDevice> vdevice,
        Hef hef, const std::string &model_name_suffix="");
    LLMInferenceManager(std::shared_ptr<hailort::VDevice> vdevice, std::shared_ptr<InferModel> model);

    virtual ~LLMInferenceManager() = default;

    virtual hailo_status generate(const std::map<std::string, MemoryView> &inputs, const std::map<std::string, MemoryView> &outputs) override;
    virtual Expected<std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>>> allocate_buffers(const std::unordered_set<std::string> &layers_not_to_allocate = {}) override;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_LLM_INFERENCE_MANAGER_HPP_ */
