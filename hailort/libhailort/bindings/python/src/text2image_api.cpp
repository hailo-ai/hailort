/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text2image_api.cpp
 * @brief Defines binding to a Text2Image class family usage over Python.
 **/

#include "text2image_api.hpp"
#include "bindings_common.hpp"

using namespace hailort;
namespace py = pybind11;


static std::vector<py::array> buffers_to_arrays(std::vector<Buffer> &&buffers, py::dtype dtype, const std::vector<size_t> &shape)
{
    std::vector<py::array> arrays;
    arrays.reserve(buffers.size());
    for (auto &buffer : buffers) {
        auto py_array = py::array(dtype, shape);
        std::memcpy(py_array.mutable_data(), buffer.data(), buffer.size());
        arrays.emplace_back(py_array);
    }
    return arrays;
}

Text2ImageWrapper Text2ImageWrapper::create(std::shared_ptr<VDeviceWrapper> vdevice, const std::string &denoise_hef, 
    const std::string &text_encoder_hef, const std::string &image_decoder_hef, genai::HailoDiffuserSchedulerType scheduler)
{
    auto text2image_params = hailort::genai::Text2ImageParams();
    VALIDATE_STATUS(text2image_params.set_denoise_model(denoise_hef));
    VALIDATE_STATUS(text2image_params.set_text_encoder_model(text_encoder_hef));
    VALIDATE_STATUS(text2image_params.set_image_decoder_model(image_decoder_hef));
    text2image_params.set_scheduler(scheduler);

    auto text2image = genai::Text2Image::create(vdevice->m_vdevice, text2image_params);
    VALIDATE_EXPECTED(text2image);

    auto text2image_ptr = std::make_unique<genai::Text2Image>(text2image.release());
    return Text2ImageWrapper(std::move(text2image_ptr));
}

Text2ImageWrapper::Text2ImageWrapper(std::unique_ptr<genai::Text2Image> text2image) :
    m_text2image(std::move(text2image))
{}

genai::Text2ImageGeneratorParams Text2ImageWrapper::create_generator_params()
{
    auto params = m_text2image->create_generator_params();
    VALIDATE_EXPECTED(params);
    return params.release();
}

std::vector<py::array> Text2ImageWrapper::generate(const genai::Text2ImageGeneratorParams &params, const std::string &positive_prompt,
    const std::string &negative_prompt, uint32_t timeout_ms)
{
    auto buffers = m_text2image->generate(params, positive_prompt, negative_prompt, std::chrono::milliseconds(timeout_ms));
    VALIDATE_EXPECTED(buffers);
    
    return buffers_to_arrays(buffers.release(), output_sample_format_type(), output_sample_shape());
}

uint32_t Text2ImageWrapper::output_sample_frame_size() const
{
    return m_text2image->output_sample_frame_size();
}

std::vector<size_t> Text2ImageWrapper::output_sample_shape() const
{
    auto shape = m_text2image->output_sample_shape();
    return { shape.height, shape.width, shape.features };
}

py::dtype Text2ImageWrapper::output_sample_format_type() const
{
    return HailoRTBindingsCommon::get_dtype(m_text2image->output_sample_format_type());
}

hailo_format_order_t Text2ImageWrapper::output_sample_format_order() const
{
    return m_text2image->output_sample_format_order();
}

std::vector<int> Text2ImageWrapper::tokenize(const std::string &prompt)
{
    auto tokens = m_text2image->tokenize(prompt);
    VALIDATE_EXPECTED(tokens);
    return tokens.release();
}

void Text2ImageWrapper::release()
{
    m_text2image.reset();
}

void Text2ImageWrapper::bind(py::module &m)
{
    // Bind the scheduler enum
    py::enum_<genai::HailoDiffuserSchedulerType>(m, "HailoDiffuserSchedulerType")
        .value("EULER_DISCRETE", genai::HailoDiffuserSchedulerType::EULER_DISCRETE)
        .value("DDIM", genai::HailoDiffuserSchedulerType::DDIM)
        ;

    // Bind Text2ImageGeneratorParams
    py::class_<genai::Text2ImageGeneratorParams>(m, "Text2ImageGeneratorParams", py::module_local())
        .def_property("samples_count",
            [](const genai::Text2ImageGeneratorParams& params) -> uint32_t {
                return params.samples_count();
            },
            [](genai::Text2ImageGeneratorParams& params, uint32_t value) {
                VALIDATE_STATUS(params.set_samples_count(value));
            })
        .def_property("steps_count",
            [](const genai::Text2ImageGeneratorParams& params) -> uint32_t {
                return params.steps_count();
            },
            [](genai::Text2ImageGeneratorParams& params, uint32_t value) {
                VALIDATE_STATUS(params.set_steps_count(value));
            })
        .def_property("guidance_scale",
            [](const genai::Text2ImageGeneratorParams& params) -> float32_t {
                return params.guidance_scale();
            },
            [](genai::Text2ImageGeneratorParams& params, float32_t value) {
                VALIDATE_STATUS(params.set_guidance_scale(value));
            })
        .def_property("seed",
            [](const genai::Text2ImageGeneratorParams& params) -> uint32_t {
                return params.seed();
            },
            [](genai::Text2ImageGeneratorParams& params, uint32_t value) {
                VALIDATE_STATUS(params.set_seed(value));
            })
        ;


    py::class_<Text2ImageWrapper, std::shared_ptr<Text2ImageWrapper>>(m, "Text2ImageWrapper")
        .def("create", &Text2ImageWrapper::create)
        .def("release", &Text2ImageWrapper::release)
        .def("create_generator_params", &Text2ImageWrapper::create_generator_params)
        .def("generate", &Text2ImageWrapper::generate)
        .def("output_sample_frame_size", &Text2ImageWrapper::output_sample_frame_size)
        .def("output_sample_shape", &Text2ImageWrapper::output_sample_shape)
        .def("output_sample_format_type", &Text2ImageWrapper::output_sample_format_type)
        .def("output_sample_format_order", &Text2ImageWrapper::output_sample_format_order)
        .def("tokenize", &Text2ImageWrapper::tokenize)
        ;
}
