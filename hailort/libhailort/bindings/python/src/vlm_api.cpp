/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vlm_api.cpp
 * @brief Defines binding to a VLM class family usage over Python.
 **/

#include "vlm_api.hpp"
#include "bindings_common.hpp"

using namespace hailort;
namespace py = pybind11;

static std::vector<MemoryView> arrays_to_memory_views(std::vector<py::array> &arrays)
{
    std::vector<MemoryView> views;
    views.reserve(arrays.size());
    for (auto &arr : arrays) {
        views.emplace_back(MemoryView(arr.mutable_data(), static_cast<size_t>(arr.nbytes())));
    }
    return views;
}

VLMWrapper VLMWrapper::create(VDeviceWrapperPtr vdevice, const std::string &model_path, bool optimize_memory_on_device)
{
    auto vlm_params = hailort::genai::VLMParams(model_path, optimize_memory_on_device);
    auto vlm = genai::VLM::create(vdevice->m_vdevice, vlm_params);
    VALIDATE_EXPECTED(vlm);

    auto vlm_ptr = std::make_unique<genai::VLM>(vlm.release());
    return VLMWrapper(std::move(vlm_ptr));
}

VLMWrapper::VLMWrapper(std::unique_ptr<genai::VLM> vlm) :
    m_vlm(std::move(vlm))
{}

genai::LLMGeneratorParams VLMWrapper::create_generator_params()
{
    auto params = m_vlm->create_generator_params();
    VALIDATE_EXPECTED(params);
    return params.release();
}

std::shared_ptr<LLMGeneratorCompletionWrapper> VLMWrapper::generate(
    const genai::LLMGeneratorParams &params,
    const std::vector<std::string> &messages_json_strings,
    std::vector<py::array> &input_frames)
{
    auto generator = m_vlm->create_generator(params);
    VALIDATE_EXPECTED(generator);
    auto completion = generator->generate(messages_json_strings, arrays_to_memory_views(input_frames));
    VALIDATE_EXPECTED(completion);
    auto completion_ptr = std::make_unique<genai::LLMGeneratorCompletion>(completion.release());
    return std::make_shared<LLMGeneratorCompletionWrapper>(std::move(completion_ptr));
}

std::shared_ptr<LLMGeneratorCompletionWrapper> VLMWrapper::generate(
    const genai::LLMGeneratorParams &params,
    const std::string &prompt,
    std::vector<py::array> &input_frames)
{
    auto generator = m_vlm->create_generator(params);
    VALIDATE_EXPECTED(generator);
    auto completion = generator->generate(prompt, arrays_to_memory_views(input_frames));
    VALIDATE_EXPECTED(completion);
    auto completion_ptr = std::make_unique<genai::LLMGeneratorCompletion>(completion.release());
    return std::make_shared<LLMGeneratorCompletionWrapper>(std::move(completion_ptr));
}

std::vector<int> VLMWrapper::tokenize(const std::string &prompt)
{
    auto expected = m_vlm->tokenize(prompt);
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

size_t VLMWrapper::get_context_usage_size()
{
    auto count = m_vlm->get_context_usage_size();
    VALIDATE_EXPECTED(count);
    return count.release();
}

size_t VLMWrapper::max_context_capacity()
{
    auto capacity = m_vlm->max_context_capacity();
    VALIDATE_EXPECTED(capacity);
    return capacity.release();
}

void VLMWrapper::clear_context()
{
    VALIDATE_STATUS(m_vlm->clear_context());
}

std::string VLMWrapper::get_generation_recovery_sequence()
{
    auto expected = m_vlm->get_generation_recovery_sequence();
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

void VLMWrapper::set_generation_recovery_sequence(const std::string &sequence)
{
    VALIDATE_STATUS(m_vlm->set_generation_recovery_sequence(sequence));
}

std::string VLMWrapper::prompt_template()
{
    auto expected = m_vlm->prompt_template();
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

void VLMWrapper::set_stop_tokens(const std::vector<std::string> &stop_tokens)
{
    VALIDATE_STATUS(m_vlm->set_stop_tokens(stop_tokens));
}

std::vector<std::string> VLMWrapper::get_stop_tokens()
{
    auto expected = m_vlm->get_stop_tokens();
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

std::vector<uint8_t> VLMWrapper::save_context()
{
    auto expected = m_vlm->save_context();
    VALIDATE_EXPECTED(expected);
    auto buffer = expected.release();
    // TODO (HRT-19106): Avoid copying the data to the Python side
    return std::vector<uint8_t>(buffer->data(), buffer->data() + buffer->size());
}

void VLMWrapper::load_context(const std::vector<uint8_t> &context)
{
    MemoryView context_view(const_cast<uint8_t*>(context.data()), context.size());
    VALIDATE_STATUS(m_vlm->load_context(context_view));
}

uint32_t VLMWrapper::input_frame_size() const
{
    return m_vlm->input_frame_size();
}

std::vector<size_t> VLMWrapper::input_frame_shape() const
{
    auto shape = m_vlm->input_frame_shape();
    return { shape.height, shape.width, shape.features };
}

py::dtype VLMWrapper::input_frame_format_type() const
{
    return HailoRTBindingsCommon::get_dtype(m_vlm->input_frame_format_type());
}

hailo_format_order_t VLMWrapper::input_frame_format_order() const
{
    return m_vlm->input_frame_format_order();
}

void VLMWrapper::release()
{
    m_vlm.reset();
}

void VLMWrapper::bind(py::module &m)
{
    py::class_<VLMWrapper, std::shared_ptr<VLMWrapper>>(m, "VLMWrapper")
        .def("create", &VLMWrapper::create)
        .def("release", &VLMWrapper::release)
        .def("create_generator_params", &VLMWrapper::create_generator_params)
        .def("generate", static_cast<std::shared_ptr<LLMGeneratorCompletionWrapper>(VLMWrapper::*)(const genai::LLMGeneratorParams&, const std::vector<std::string>&, std::vector<py::array>&)>(&VLMWrapper::generate))
        .def("generate", static_cast<std::shared_ptr<LLMGeneratorCompletionWrapper>(VLMWrapper::*)(const genai::LLMGeneratorParams&, const std::string&, std::vector<py::array>&)>(&VLMWrapper::generate))
        .def("tokenize", &VLMWrapper::tokenize)
        .def("clear_context", &VLMWrapper::clear_context)
        .def("get_generation_recovery_sequence", &VLMWrapper::get_generation_recovery_sequence)
        .def("set_generation_recovery_sequence", &VLMWrapper::set_generation_recovery_sequence)
        .def("prompt_template", &VLMWrapper::prompt_template)
        .def("set_stop_tokens", &VLMWrapper::set_stop_tokens)
        .def("get_stop_tokens", &VLMWrapper::get_stop_tokens)
        .def("save_context", &VLMWrapper::save_context)
        .def("load_context", &VLMWrapper::load_context)
        .def("input_frame_size", &VLMWrapper::input_frame_size)
        .def("input_frame_shape", &VLMWrapper::input_frame_shape)
        .def("input_frame_format_type", &VLMWrapper::input_frame_format_type)
        .def("input_frame_format_order", &VLMWrapper::input_frame_format_order)
        .def("get_context_usage_size", &VLMWrapper::get_context_usage_size)
        .def("max_context_capacity", &VLMWrapper::max_context_capacity)
        ;
}


