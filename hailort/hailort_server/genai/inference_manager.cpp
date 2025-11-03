/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inference_manager.cpp
 * @brief Implementation for genai inference manager
 **/

#include "inference_manager.hpp"
#include "common/utils.hpp"
#include "hailo/hailort_defaults.hpp"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<InferenceManager>> InferenceManager::create(std::shared_ptr<hailort::VDevice> vdevice,
    Hef hef, const std::string &model_name)
{
    TRY(auto model, vdevice->create_infer_model(hef, model_name));

    auto ptr = std::make_unique<InferenceManager>(vdevice, model);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status
    return ptr;
}

InferenceManager::InferenceManager(std::shared_ptr<hailort::VDevice> vdevice, std::shared_ptr<InferModel> model) :
    m_vdevice(vdevice), m_model(model)
{}

hailo_status InferenceManager::configure()
{
    TRY(m_configured_model, m_model->configure());
    TRY(m_bindings, m_configured_model.create_bindings());

    return HAILO_SUCCESS;
}

Expected<std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>>> InferenceManager::allocate_buffers(const std::unordered_set<std::string> &layers_not_to_allocate)
{
    TRY(auto input_buffers, allocate_input_buffers(layers_not_to_allocate));
    TRY(auto output_buffers, allocate_output_buffers(layers_not_to_allocate));
    return std::make_pair(input_buffers, output_buffers);
}

Expected<std::map<std::string, BufferPtr>> InferenceManager::allocate_input_buffers(const std::unordered_set<std::string> &layers_not_to_allocate)
{
    std::map<std::string, BufferPtr> input_buffers;
    for (const auto &input : m_model->inputs()) {
        if (contains(layers_not_to_allocate, input.name())) {
            continue;
        }
        TRY(auto buffer, Buffer::create_shared(input.get_frame_size(), BufferStorageParams::create_dma()));
        input_buffers[input.name()] = buffer;
    }
    return input_buffers;
}

Expected<std::map<std::string, BufferPtr>> InferenceManager::allocate_output_buffers(const std::unordered_set<std::string> &layers_not_to_allocate)
{
    std::map<std::string, BufferPtr> output_buffers;
    for (const auto &output : m_model->outputs()) {
        if (contains(layers_not_to_allocate, output.name())) {
            continue;
        }
        TRY(auto buffer, Buffer::create_shared(output.get_frame_size(), BufferStorageParams::create_dma()));
        output_buffers[output.name()] = buffer;
    }
    return output_buffers;
}

hailo_status InferenceManager::generate(const std::map<std::string, MemoryView> &inputs, const std::map<std::string, MemoryView> &outputs)
{
    std::map<std::string, MemoryView> all_buffers;
    all_buffers.insert(inputs.begin(), inputs.end());
    all_buffers.insert(outputs.begin(), outputs.end());

    TRY(auto bindings, m_configured_model.create_bindings(all_buffers));

    return m_configured_model.run(bindings, INFERENCE_TIMEOUT);
}

hailo_status InferenceManager::generate()
{
    return m_configured_model.run(m_bindings, INFERENCE_TIMEOUT);
}

Expected<AsyncInferJob> InferenceManager::generate_async()
{
    return m_configured_model.run_async(m_bindings);
}

Expected<AsyncInferJob> InferenceManager::generate_async(MemoryView input, MemoryView output)
{
    TRY(auto bindings, m_configured_model.create_bindings());

    TRY(auto input_config, bindings.input());
    CHECK_SUCCESS(input_config.set_buffer(input));

    TRY(auto output_config, bindings.output());
    CHECK_SUCCESS(output_config.set_buffer(output));

    return m_configured_model.run_async(bindings);
}

hailo_status InferenceManager::generate(MemoryView input, MemoryView output)
{
    TRY(auto job, generate_async(input, output));
    return job.wait(INFERENCE_TIMEOUT);
}

hailo_status InferenceManager::set_input_buffer(MemoryView buffer, const std::string &name)
{
    return m_bindings.input(name)->set_buffer(buffer);
}

hailo_status InferenceManager::set_output_buffer(MemoryView buffer, const std::string &name)
{
    return m_bindings.output(name)->set_buffer(buffer);
}

hailo_status InferenceManager::set_input_buffer(MemoryView buffer)
{
    TRY(auto input, m_bindings.input());
    return input.set_buffer(buffer);
}

hailo_status InferenceManager::set_output_buffer(MemoryView buffer)
{
    TRY(auto output, m_bindings.output());
    return output.set_buffer(buffer);
}

hailo_status InferenceManager::update_cache_offset(int32_t offset_delta_entries)
{
    return m_configured_model.update_cache_offset(offset_delta_entries);
}

hailo_status InferenceManager::init_cache(uint32_t read_offset)
{
    return m_configured_model.init_cache(read_offset);
}

Expected<std::unordered_map<uint32_t, BufferPtr>> InferenceManager::get_cache_buffers()
{
    return m_configured_model.get_cache_buffers();
}

hailo_status InferenceManager::update_cache_buffer(uint32_t cache_id, MemoryView buffer)
{
    return m_configured_model.update_cache_buffer(cache_id, buffer);
}

const Hef &InferenceManager::get_hef() const
{
    return m_model->hef();
}

std::map<std::string, size_t> InferenceManager::get_inputs_frame_size() const
{
    std::map<std::string, size_t> inputs_frame_size;
    for (const auto &input : m_model->inputs()) {
        inputs_frame_size.emplace(input.name(), input.get_frame_size());
    }

    return inputs_frame_size;
}

std::shared_ptr<InferModel> InferenceManager::get_model()
{
    return m_model;
}

ConfiguredInferModel& InferenceManager::get_configured_model()
{
    return m_configured_model;
}

} /* namespace genai */
} /* namespace hailort */
