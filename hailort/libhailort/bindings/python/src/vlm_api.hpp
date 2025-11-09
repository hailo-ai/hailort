/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm_api.hpp
 * @brief Defines binding to a VLM class family usage over Python.
 **/

#ifndef VLM_API_HPP_
#define VLM_API_HPP_

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "hailo/hailort.h"
#include "hailo/genai/vlm/vlm.hpp"
#include "hailo/genai/llm/llm.hpp"

#include "vdevice_api.hpp"
#include "llm_api.hpp"

namespace hailort {

class VLMWrapper final {
public:
    static VLMWrapper create(std::shared_ptr<VDeviceWrapper> vdevice, const std::string &model_path, bool optimize_memory_on_device = false);

    VLMWrapper(std::unique_ptr<genai::VLM> vlm);

    genai::LLMGeneratorParams create_generator_params();

    std::shared_ptr<LLMGeneratorCompletionWrapper> generate(const genai::LLMGeneratorParams &params,
        const std::vector<std::string> &messages_json_strings,
        std::vector<py::array> &input_frames);

    std::shared_ptr<LLMGeneratorCompletionWrapper> generate(const genai::LLMGeneratorParams &params,
        const std::string &prompt, std::vector<py::array> &input_frames);

    // Misc controls
    std::vector<int> tokenize(const std::string &prompt);
    size_t get_context_usage_size();
    size_t max_context_capacity();
    void clear_context();
    std::string get_generation_recovery_sequence();
    void set_generation_recovery_sequence(const std::string &sequence);
    std::string prompt_template();
    void set_stop_tokens(const std::vector<std::string> &stop_tokens);
    std::vector<std::string> get_stop_tokens();
    std::vector<uint8_t> save_context();
    void load_context(const std::vector<uint8_t> &context);

    // Input frame metadata
    uint32_t input_frame_size() const;
    std::vector<size_t> input_frame_shape() const;
    py::dtype input_frame_format_type() const;
    hailo_format_order_t input_frame_format_order() const;

    void release();

    static void bind(py::module &m);

private:
    std::unique_ptr<genai::VLM> m_vlm;
};

} // namespace hailort

#endif /* VLM_API_HPP_ */


