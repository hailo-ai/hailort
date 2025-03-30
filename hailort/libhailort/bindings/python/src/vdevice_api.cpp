/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model_api.cpp
 * @brief Defines binding to a infer model class family usage over Python.
 **/
#include "vdevice_api.hpp"
#include "infer_model_api.hpp"

using namespace hailort;

InferModelWrapper VDeviceWrapper::create_infer_model_from_file(const std::string &hef_path, const std::string &name)
{
    auto infer_model = m_vdevice->create_infer_model(hef_path, name);
    VALIDATE_EXPECTED(infer_model);

    return InferModelWrapper(infer_model.release(), m_is_using_service);
}

InferModelWrapper VDeviceWrapper::create_infer_model_from_buffer(const py::bytes &buffer, const std::string &name)
{
    // there are 3 ways to get the buffer from python and convert it to MemoryView:
    // 1. py::bytes -> std::string -> MemoryView
    // 2. py::bytes -> py::buffer -> MemoryView (this is the one used here)
    // 3. std::string -> MemoryView
    //
    // 1+3 are copying the data, while 2 isn't, resulting in 700X faster transfer between python and c++ (tested on yolov5s [~15MB])
    py::buffer_info info(py::buffer(buffer).request());
    MemoryView hef_buffer(MemoryView(info.ptr, static_cast<size_t>(info.size)));
    auto infer_model = m_vdevice->create_infer_model(hef_buffer, name);
    VALIDATE_EXPECTED(infer_model);

    return InferModelWrapper(infer_model.release(), m_is_using_service);
}
