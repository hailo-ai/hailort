/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text2image_api.hpp
 * @brief Defines binding to a Text2Image class family usage over Python.
 **/

#ifndef TEXT2IMAGE_API_HPP_
#define TEXT2IMAGE_API_HPP_

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "hailo/hailort.h"
#include "hailo/genai/text2image/text2image.hpp"

#include "vdevice_api.hpp"

namespace hailort {

class Text2ImageWrapper final {
public:
    static Text2ImageWrapper create(std::shared_ptr<VDeviceWrapper> vdevice, const std::string &denoise_hef,
                                    const std::string &text_encoder_hef, const std::string &image_decoder_hef,
                                    genai::HailoDiffuserSchedulerType scheduler = genai::HailoDiffuserSchedulerType::EULER_DISCRETE);

    Text2ImageWrapper(std::unique_ptr<genai::Text2Image> text2image);

    genai::Text2ImageGeneratorParams create_generator_params();

    std::vector<py::array> generate(const genai::Text2ImageGeneratorParams &params, const std::string &positive_prompt,
        const std::string &negative_prompt, uint32_t timeout_ms);

    // Metadata functions
    uint32_t output_sample_frame_size() const;
    std::vector<size_t> output_sample_shape() const;
    py::dtype output_sample_format_type() const;
    hailo_format_order_t output_sample_format_order() const;

    std::vector<int> tokenize(const std::string &prompt);

    void release();

    static void bind(py::module &m);

private:
    std::unique_ptr<genai::Text2Image> m_text2image;
};

} // namespace hailort

#endif /* TEXT2IMAGE_API_HPP_ */
