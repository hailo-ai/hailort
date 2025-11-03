/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file speech2text_api.hpp
 * @brief Defines binding to a Speech2Text class family usage over Python.
 **/

#ifndef SPEECH2TEXT_API_HPP_
#define SPEECH2TEXT_API_HPP_

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "hailo/hailort.h"
#include "hailo/genai/speech2text/speech2text.hpp"

#include "vdevice_api.hpp"

namespace hailort {

class Speech2TextWrapper final {
public:
    static Speech2TextWrapper create(VDeviceWrapperPtr vdevice, const std::string &model_path);

    Speech2TextWrapper(std::unique_ptr<genai::Speech2Text> speech2text);

    std::string generate_all_text(py::array audio_data, genai::Speech2TextTask task, std::string_view language,
        uint32_t timeout_ms);

    std::vector<genai::Speech2Text::SegmentInfo> generate_all_segments(py::array audio_data, genai::Speech2TextTask task,
        std::string_view language, uint32_t timeout_ms);

    std::vector<int> tokenize(const std::string &text);

    void release();

    static void bind(py::module &m);

private:
    std::unique_ptr<genai::Speech2Text> m_speech2text;
};

} // namespace hailort

#endif /* SPEECH2TEXT_API_HPP_ */
