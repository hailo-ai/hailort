/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm_inference_manager.cpp
 * @brief Implementation for LLM inference manager
 **/

#include "llm_inference_manager.hpp"
#include "common/utils.hpp"
#include "utils.hpp"
#include "hailo/hailort_defaults.hpp"

namespace hailort
{
namespace genai
{

static const std::string OUTPUT_NAME = "output";

Expected<std::unique_ptr<InferenceManager>> LLMInferenceManager::create(std::shared_ptr<hailort::VDevice> vdevice,
    Hef hef, const std::string &model_name_suffix)
{
    std::string model_name = "";
    for (const auto &network_group_name : hef.get_network_groups_names()) {
        if (has_suffix(network_group_name, model_name_suffix)) {
            model_name = network_group_name;
        }
    }
    CHECK(!model_name.empty(), HAILO_INTERNAL_FAILURE, "Model doesnt have NG with name-suffix '{}'", model_name_suffix);
    TRY(auto model, vdevice->create_infer_model(hef, model_name));

    model->set_enable_kv_cache(true);

    auto ptr = std::make_unique<LLMInferenceManager>(vdevice, model);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return std::unique_ptr<InferenceManager>(std::move(ptr));
}

LLMInferenceManager::LLMInferenceManager(std::shared_ptr<hailort::VDevice> vdevice, std::shared_ptr<InferModel> model) :
    InferenceManager(vdevice, model)
{}

Expected<std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>>> LLMInferenceManager::allocate_buffers(const std::unordered_set<std::string> &layers_not_to_allocate)
{
    std::map<std::string, BufferPtr> input_buffers;
    for (const auto &input : m_model->inputs()) {
        if (contains(layers_not_to_allocate, input.name())) {
            continue;
        }
        TRY(auto buffer, Buffer::create_shared(input.get_frame_size(), BufferStorageParams::create_dma()));
        input_buffers[input.name()] = buffer;
    }

    std::map<std::string, BufferPtr> output_buffers;
    // TODO (HRT-16650): concatinate the multiple outputs inside libhailort
    size_t acc_size = 0;
    for (const auto &output : m_model->outputs()) {
        if (contains(layers_not_to_allocate, output.name())) {
            continue;
        }
        acc_size += output.get_frame_size();
    }
    TRY(auto buffer, Buffer::create_shared(acc_size, BufferStorageParams::create_dma()));
    output_buffers[OUTPUT_NAME] = buffer;

    return std::make_pair(input_buffers, output_buffers);
}

hailo_status LLMInferenceManager::generate(const std::map<std::string, MemoryView> &inputs, const std::map<std::string, MemoryView> &outputs)
{
    std::map<std::string, MemoryView> all_buffers;
    all_buffers.insert(inputs.begin(), inputs.end());

    // TODO (HRT-16650): concatinate the multiple outputs inside libhailort
    auto output_buffer_ptr = outputs.at(OUTPUT_NAME).data();
    int i = 0;
    size_t output_size = 0;
    for (auto output : m_model->outputs()) {
        assert((output_size == output.get_frame_size()) || (0 == output_size)); // Make sure all outputs have the same size
        output_size = output.get_frame_size();
        all_buffers[output.name()] = MemoryView(const_cast<uint8_t*>(output_buffer_ptr + (output_size * i++)), output_size);
    }
    TRY(auto bindings, m_configured_model.create_bindings(all_buffers));

    return m_configured_model.run(bindings, INFERENCE_TIMEOUT);
}

} /* namespace genai */
} /* namespace hailort */
