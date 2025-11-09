/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text_api.cpp
 * @brief Defines binding to a Speech2Text class family usage over Python.
 **/

#include "speech2text_api.hpp"
#include "bindings_common.hpp"

using namespace hailort;

Speech2TextWrapper Speech2TextWrapper::create(VDeviceWrapperPtr vdevice, const std::string &model_path)
{
    auto speech2text_params = genai::Speech2TextParams(model_path);
    auto expected_speech2text = genai::Speech2Text::create(vdevice->m_vdevice, speech2text_params);
    VALIDATE_EXPECTED(expected_speech2text);

    auto speech2text_ptr = std::make_unique<genai::Speech2Text>(expected_speech2text.release());

    return Speech2TextWrapper(std::move(speech2text_ptr));
}

Speech2TextWrapper::Speech2TextWrapper(std::unique_ptr<genai::Speech2Text> speech2text) :
    m_speech2text(std::move(speech2text))
{}

std::string Speech2TextWrapper::generate_all_text(py::array audio_data,
    genai::Speech2TextTask task, std::string_view language, uint32_t timeout_ms)
{
    // Convert numpy array to MemoryView
    auto buffer_info = audio_data.request();
    if (buffer_info.ndim != 1) {
        throw std::invalid_argument("Audio data must be 1-dimensional");
    }

    // Ensure the data is contiguous
    if (!py::array_t<float32_t>::check_(audio_data)) {
        throw std::invalid_argument("Audio data must be float32 type");
    }

    auto audio_buffer = hailort::MemoryView(audio_data.mutable_data(), static_cast<size_t>(audio_data.nbytes()));

    auto generator_params = genai::Speech2TextGeneratorParams(task, language);
    auto expected_result = m_speech2text->generate_all_text(audio_buffer, generator_params, std::chrono::milliseconds(timeout_ms));
    VALIDATE_EXPECTED(expected_result);
    return expected_result.release();
}

std::vector<genai::Speech2Text::SegmentInfo> Speech2TextWrapper::generate_all_segments(
    py::array audio_data, genai::Speech2TextTask task, std::string_view language, uint32_t timeout_ms)
{
    // Convert numpy array to MemoryView
    auto buffer_info = audio_data.request();
    if (buffer_info.ndim != 1) {
        throw std::invalid_argument("Audio data must be 1-dimensional");
    }

    // Ensure the data is contiguous
    if (!py::array_t<float32_t>::check_(audio_data)) {
        throw std::invalid_argument("Audio data must be float32 type");
    }

    auto audio_buffer = hailort::MemoryView(audio_data.mutable_data(), static_cast<size_t>(audio_data.nbytes()));

    auto generator_params = genai::Speech2TextGeneratorParams(task, language);
    auto expected_result = m_speech2text->generate_all_segments(audio_buffer, generator_params,
        std::chrono::milliseconds(timeout_ms));
    VALIDATE_EXPECTED(expected_result);
    return expected_result.release();
}

std::vector<int> Speech2TextWrapper::tokenize(const std::string &text)
{   
    auto expected_result = m_speech2text->tokenize(text);
    VALIDATE_EXPECTED(expected_result);
    return expected_result.release();
}

void Speech2TextWrapper::release()
{
    m_speech2text.reset();
}

void Speech2TextWrapper::bind(py::module &m)
{
    // Bind SegmentInfo struct
    py::class_<genai::Speech2Text::SegmentInfo>(m, "SegmentInfo")
        .def_readonly("start_sec", &genai::Speech2Text::SegmentInfo::start_sec)
        .def_readonly("end_sec", &genai::Speech2Text::SegmentInfo::end_sec)
        .def_readonly("text", &genai::Speech2Text::SegmentInfo::text)
        .def("__repr__", [](const genai::Speech2Text::SegmentInfo &self) {
            return "SegmentInfo(start_sec=" + std::to_string(self.start_sec) + 
                   ", end_sec=" + std::to_string(self.end_sec) + 
                   ", text='" + self.text + "')";
        });

    // Bind Speech2TextTask enum
    py::enum_<genai::Speech2TextTask>(m, "Speech2TextTask")
        .value("TRANSCRIBE", genai::Speech2TextTask::TRANSCRIBE)
        .value("TRANSLATE", genai::Speech2TextTask::TRANSLATE);

    // Bind Speech2TextWrapper
    py::class_<Speech2TextWrapper, std::shared_ptr<Speech2TextWrapper>>(m, "Speech2Text")
        .def_static("create", &Speech2TextWrapper::create)
        .def("generate_all_text", &Speech2TextWrapper::generate_all_text)
        .def("generate_all_segments", &Speech2TextWrapper::generate_all_segments)
        .def("tokenize", &Speech2TextWrapper::tokenize)
        .def("release", &Speech2TextWrapper::release);
}
