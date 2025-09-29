/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file decoder_inference_manager.hpp
 * @brief Implementation for decoder inference manager
 **/

#ifndef _HAILO_GENAI_SPEECH2TEXT_DECODER_INFERENCE_MANAGER_HPP_
#define _HAILO_GENAI_SPEECH2TEXT_DECODER_INFERENCE_MANAGER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/infer_model.hpp"
#include "inference_manager.hpp"

namespace hailort
{
namespace genai
{

// Concatenated embeddings output
static const std::string CONCATENATED_OUTPUT_NAME = "concatenated_output";

// TODO: HRT-18789 - Use it in LLM and remove LLMInferenceManager
class DecoderInferenceManager : public InferenceManager
{
public:
    static Expected<std::unique_ptr<DecoderInferenceManager>> create(std::shared_ptr<hailort::VDevice> vdevice,
        Hef hef, const std::string &model_name, const std::vector<std::string> &outputs_to_concat);
    DecoderInferenceManager(std::shared_ptr<hailort::VDevice> vdevice, std::shared_ptr<InferModel> model,
        const std::vector<std::string> &outputs_to_concat);

    virtual ~DecoderInferenceManager() = default;

    virtual hailo_status generate(const std::map<std::string, MemoryView> &inputs, const std::map<std::string, MemoryView> &outputs) override;

    // This function is:
    // 1. Allocating the input buffers, except the inputs in `layers_not_to_allocate` (black list of inputs and outputs)
    // 2. Allocating the outputs in `outputs_to_concat` as one buffer
    // 3. Allocating the rest of the outputs, except the ones in `layers_not_to_allocate`
    using InferenceManager::allocate_buffers; // exposes the base function - so it won't be hidden
    Expected<std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>>> allocate_buffers(
        const std::vector<std::string> &layers_not_to_allocate);

private:
    const std::vector<std::string> m_outputs_to_concat;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_SPEECH2TEXT_DECODER_INFERENCE_MANAGER_HPP_ */
