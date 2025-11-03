/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inference_manager.hpp
 * @brief Implementation for genai inference manager
 **/

#ifndef _HAILO_HAILO_GENAI_INFERENCE_MANAGER_HPP_
#define _HAILO_HAILO_GENAI_INFERENCE_MANAGER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/infer_model.hpp"
#include <unordered_set>


namespace hailort
{
namespace genai
{

const auto INFERENCE_TIMEOUT = std::chrono::seconds(15); // TODO Consider changing this timeout

class InferenceManager
{
public:
    static Expected<std::unique_ptr<InferenceManager>> create(std::shared_ptr<hailort::VDevice> vdevice,
        Hef hef, const std::string &model_name="");
    InferenceManager(std::shared_ptr<hailort::VDevice> vdevice, std::shared_ptr<InferModel> model);

    virtual ~InferenceManager() = default;

    virtual hailo_status generate(const std::map<std::string, MemoryView> &inputs, const std::map<std::string, MemoryView> &outputs);
    hailo_status generate();
    Expected<AsyncInferJob> generate_async();
    Expected<AsyncInferJob> generate_async(MemoryView input, MemoryView output);
    hailo_status generate(MemoryView input, MemoryView output);
    hailo_status set_input_buffer(MemoryView buffer, const std::string &name);
    hailo_status set_output_buffer(MemoryView buffer, const std::string &name);

    // Will return error if model has more the one input or output
    hailo_status set_input_buffer(MemoryView buffer);
    hailo_status set_output_buffer(MemoryView buffer);

    virtual Expected<std::pair<std::map<std::string, BufferPtr>, std::map<std::string, BufferPtr>>> allocate_buffers(const std::unordered_set<std::string> &layers_not_to_allocate = {});
    Expected<std::map<std::string, BufferPtr>> allocate_input_buffers(const std::unordered_set<std::string> &layers_not_to_allocate = {});
    Expected<std::map<std::string, BufferPtr>> allocate_output_buffers(const std::unordered_set<std::string> &layers_not_to_allocate = {});

    hailo_status configure();
    hailo_status update_cache_offset(int32_t offset_delta_entries);
    hailo_status init_cache(uint32_t read_offset);

    Expected<std::unordered_map<uint32_t, BufferPtr>> get_cache_buffers();
    hailo_status update_cache_buffer(uint32_t cache_id, MemoryView buffer);

    const Hef &get_hef() const;
    std::map<std::string, size_t> get_inputs_frame_size() const;
    std::shared_ptr<InferModel> get_model();
    ConfiguredInferModel& get_configured_model();

protected:
    std::shared_ptr<hailort::VDevice> m_vdevice;
    std::shared_ptr<InferModel> m_model;
    ConfiguredInferModel m_configured_model;
    ConfiguredInferModel::Bindings m_bindings;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_INFERENCE_MANAGER_HPP_ */
