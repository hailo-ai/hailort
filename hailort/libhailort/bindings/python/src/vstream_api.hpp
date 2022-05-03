/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream_api.hpp
 * @brief Defines binding to virtual stream usage over Python.
 **/

#ifndef _VSTREAM_API_HPP_
#define _VSTREAM_API_HPP_

#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "hailo/vstream.hpp"
#include "hailo/inference_pipeline.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

namespace hailort
{

class InputVStreamWrapper final
{
public:
    static void add_to_python_module(py::module &m);
};

class InputVStreamsWrapper final 
{
public:
    static InputVStreamsWrapper create(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &input_vstreams_params);
    const InputVStreamsWrapper &enter();
    void exit();
    std::shared_ptr<InputVStream> get_input_by_name(const std::string &name);
    py::dict get_all_inputs();
    void clear();
    static void add_to_python_module(py::module &m);

private:
    InputVStreamsWrapper(std::unordered_map<std::string, std::shared_ptr<InputVStream>> &input_vstreams);
    std::unordered_map<std::string, std::shared_ptr<InputVStream>> m_input_vstreams;
};

class OutputVStreamWrapper final
{
public:
    static py::dtype get_dtype(OutputVStream &self);
    static hailo_format_t get_user_buffer_format(OutputVStream &self);
    static auto get_shape(OutputVStream &self);
    static void add_to_python_module(py::module &m);
};

class OutputVStreamsWrapper final
{
public:
    static OutputVStreamsWrapper create(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &output_vstreams_params);
    std::shared_ptr<OutputVStream> get_output_by_name(const std::string &name);
    const OutputVStreamsWrapper &enter();
    void exit();
    py::dict get_all_outputs();
    void clear();
    static void add_to_python_module(py::module &m);

private:
    OutputVStreamsWrapper(std::unordered_map<std::string, std::shared_ptr<OutputVStream>> &output_vstreams);
    std::unordered_map<std::string, std::shared_ptr<OutputVStream>> m_output_vstreams;
};

class InferVStreamsWrapper final
{
public:
    static InferVStreamsWrapper create(ConfiguredNetworkGroup &network_group,
        const std::map<std::string, hailo_vstream_params_t> &input_vstreams_params,
        const std::map<std::string, hailo_vstream_params_t> &output_vstreams_params);
    void infer(std::map<std::string, py::array> input_data, std::map<std::string, py::array> output_data,
        size_t batch_size);
    py::dtype get_host_dtype(const std::string &stream_name);
    hailo_format_t get_user_buffer_format(const std::string &stream_name);
    std::vector<size_t> get_shape(const std::string &stream_name);
    void release();
    static void add_to_python_module(py::module &m);

private:
    InferVStreamsWrapper(std::shared_ptr<InferVStreams> &infer_pipeline);
    
    std::shared_ptr<InferVStreams> m_infer_pipeline;
};

void VStream_api_initialize_python_module(py::module &m);
} /* namespace hailort */

#endif // _VSTREAM_API_HPP_
