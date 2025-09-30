/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file decoder_inference_manager.cpp
 * @brief Implementation for decoder inference manager
 **/

#include "decoder_inference_manager.hpp"
#include "common/utils.hpp"
#include "utils.hpp"
#include "hailo/hailort_defaults.hpp"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<DecoderInferenceManager>> DecoderInferenceManager::create(std::shared_ptr<hailort::VDevice> vdevice,
    Hef hef, const std::string &model_name, const std::vector<std::string> &outputs_to_concat)
{
    TRY(auto model, vdevice->create_infer_model(hef, model_name));

    auto ptr = std::make_unique<DecoderInferenceManager>(vdevice, model, outputs_to_concat);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return ptr;
}

DecoderInferenceManager::DecoderInferenceManager(std::shared_ptr<hailort::VDevice> vdevice, std::shared_ptr<InferModel> model,
    const std::vector<std::string> &outputs_to_concat) :
    InferenceManager(vdevice, model), m_outputs_to_concat(outputs_to_concat)
{}

Expected<std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>>> DecoderInferenceManager::allocate_buffers(
    const std::vector<std::string> &layers_not_to_allocate)
{
    std::map<std::string, BufferPtr> input_buffers;
    std::map<std::string, BufferPtr> output_buffers;

    // Allocate input buffers, except the inputs in layers_not_to_allocate
    for (const auto &input : m_model->inputs()) {
        if (contains(layers_not_to_allocate, input.name())) {
            continue;
        }
        TRY(auto buffer, Buffer::create_shared(input.get_frame_size(), BufferStorageParams::create_dma()));
        input_buffers[input.name()] = buffer;
    }

    // Allocate the outputs in `outputs_to_concat` as one buffer
    size_t acc_size = 0;
    for (const auto &output_name : m_outputs_to_concat) {
        TRY(auto output, m_model->output(output_name));
        acc_size += output.get_frame_size();
    }
    TRY(auto buffer, Buffer::create_shared(acc_size, BufferStorageParams::create_dma()));
    output_buffers[CONCATENATED_OUTPUT_NAME] = buffer;

    // Allocate the rest of the outputs, except the ones in `layers_not_to_allocate`
    for (const auto &output : m_model->outputs()) {
        if (contains(m_outputs_to_concat, output.name()) || contains(layers_not_to_allocate, output.name())) {
            continue;
        }
        TRY(auto buffer, Buffer::create_shared(output.get_frame_size(), BufferStorageParams::create_dma()));
        output_buffers[output.name()] = buffer;
    }

    return std::make_pair(input_buffers, output_buffers);
}

hailo_status DecoderInferenceManager::generate(const std::map<std::string, MemoryView> &inputs, const std::map<std::string, MemoryView> &outputs)
{
    std::map<std::string, MemoryView> all_buffers;
    all_buffers.insert(inputs.begin(), inputs.end());

    auto output_buffer_ptr = outputs.at(CONCATENATED_OUTPUT_NAME).data();
    size_t acc_size = 0;
    for (auto output : m_model->outputs()) {
        if (contains(m_outputs_to_concat, output.name())) {
            // Note: Not all concatenated outputs must be the same size
            auto output_size = output.get_frame_size();
            assert(acc_size + output_size <= outputs.at(CONCATENATED_OUTPUT_NAME).size());
            all_buffers[output.name()] = MemoryView(const_cast<uint8_t*>(output_buffer_ptr + acc_size), output_size);
            acc_size += output_size;
        } else {
            all_buffers[output.name()] = outputs.at(output.name());
        }
    }
    TRY(auto bindings, m_configured_model.create_bindings(all_buffers));

    return m_configured_model.run(bindings, INFERENCE_TIMEOUT);
}

} /* namespace genai */
} /* namespace hailort */
