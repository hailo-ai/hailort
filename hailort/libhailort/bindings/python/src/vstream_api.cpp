/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream_api.cpp
 * @brief Implementation of binding to virtual stream usage over Python.
 **/

#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include "vstream_api.hpp"
#include "bindings_common.hpp"
#include "utils.hpp"


namespace hailort
{

void InputVStreamWrapper::add_to_python_module(py::module &m)
{
    py::class_<InputVStream, std::shared_ptr<InputVStream>>(m, "InputVStream")
    .def("send", [](InputVStream &self, py::array data)
    {
        hailo_status status = self.write(
            MemoryView(const_cast<void*>(reinterpret_cast<const void*>(data.data())), data.nbytes()));
        VALIDATE_STATUS(status);
    })
    .def("flush", [](InputVStream &self)
    {
        hailo_status status = self.flush();
        VALIDATE_STATUS(status);
    })
    .def_property_readonly("info", [](InputVStream &self)
    {
        return self.get_info();
    })
    .def_property_readonly("dtype", [](InputVStream &self)
    {
        const auto format_type = self.get_user_buffer_format().type;
        return HailoRTBindingsCommon::get_dtype(format_type);
    })
    .def_property_readonly("shape", [](InputVStream &self)
    {
        return *py::array::ShapeContainer(HailoRTBindingsCommon::get_pybind_shape(self.get_info(), self.get_user_buffer_format()));
    })
    ;
}

InputVStreamsWrapper InputVStreamsWrapper::create(ConfiguredNetworkGroup &net_group,
    const std::map<std::string, hailo_vstream_params_t> &input_vstreams_params)
{
    auto input_vstreams_expected = VStreamsBuilder::create_input_vstreams(net_group, input_vstreams_params);
    VALIDATE_STATUS(input_vstreams_expected.status());

    std::unordered_map<std::string, std::shared_ptr<InputVStream>> input_vstreams;
    for (auto &input : input_vstreams_expected.value()) {
        input_vstreams.emplace(input.name(), make_shared_nothrow<InputVStream>(std::move(input)));
    }
    return InputVStreamsWrapper(input_vstreams);
}

const InputVStreamsWrapper &InputVStreamsWrapper::enter()
{
    return std::ref(*this);
}

void InputVStreamsWrapper::exit()
{
    m_input_vstreams.clear();
}

std::shared_ptr<InputVStream> InputVStreamsWrapper::get_input_by_name(const std::string &name) 
{
    auto input = m_input_vstreams.find(name);
    if (m_input_vstreams.end() == input) {
        LOGGER__ERROR("Input virtual stream for name={} not found", name);
        THROW_STATUS_ERROR(HAILO_NOT_FOUND);
    }

    return input->second;
}

py::dict InputVStreamsWrapper::get_all_inputs()
{
    return py::cast(m_input_vstreams);
}

void InputVStreamsWrapper::clear()
{
    std::vector<std::reference_wrapper<InputVStream>> inputs;
    inputs.reserve(m_input_vstreams.size());
    for (auto &name_vstream_pair : m_input_vstreams) {
        inputs.emplace_back(std::ref(*name_vstream_pair.second));
    }
    
    auto status = InputVStream::clear(inputs);
    VALIDATE_STATUS(status);
}

void InputVStreamsWrapper::add_to_python_module(py::module &m)
{
    py::class_<InputVStreamsWrapper>(m, "InputVStreams")
    .def(py::init(&InputVStreamsWrapper::create))
    .def("get_input_by_name", &InputVStreamsWrapper::get_input_by_name)
    .def("get_all_inputs", &InputVStreamsWrapper::get_all_inputs)
    .def("clear", &InputVStreamsWrapper::clear)
    .def("__enter__", &InputVStreamsWrapper::enter, py::return_value_policy::reference)
    .def("__exit__",  [&](InputVStreamsWrapper &self, py::args) { self.exit(); })
    ;
}

InputVStreamsWrapper::InputVStreamsWrapper(std::unordered_map<std::string, std::shared_ptr<InputVStream>> &input_vstreams)
    : m_input_vstreams(std::move(input_vstreams))
{}

py::dtype OutputVStreamWrapper::get_dtype(OutputVStream &self)
{
    const auto format_type = self.get_user_buffer_format().type;
    return HailoRTBindingsCommon::get_dtype(format_type);
}

hailo_format_t OutputVStreamWrapper::get_user_buffer_format(OutputVStream &self)
{
    const auto format = self.get_user_buffer_format();
    return format;
}

auto OutputVStreamWrapper::get_shape(OutputVStream &self)
{
    return *py::array::ShapeContainer(HailoRTBindingsCommon::get_pybind_shape(self.get_info(), self.get_user_buffer_format()));
}

void OutputVStreamWrapper::add_to_python_module(py::module &m)
{
    py::class_<OutputVStream, std::shared_ptr<OutputVStream>>(m, "OutputVStream")
    .def("recv", [](OutputVStream &self)
    {
        auto buffer = Buffer::create(self.get_frame_size());
        VALIDATE_STATUS(buffer.status());

        hailo_status status = self.read(MemoryView(buffer->data(), buffer->size()));
        VALIDATE_STATUS(status);

        // Note: The ownership of the buffer is transferred to Python wrapped as a py::array.
        //       When the py::array isn't referenced anymore in Python and is destructed, the py::capsule's dtor
        //       is called too (and it deletes the raw buffer)
        const auto unmanaged_addr = buffer.release().release();
        return py::array(get_dtype(self), get_shape(self), unmanaged_addr,
            py::capsule(unmanaged_addr, [](void *p) { delete reinterpret_cast<uint8_t*>(p); }));
    })
    .def_property_readonly("info", [](OutputVStream &self)
    {
        return self.get_info();
    })
    .def_property_readonly("dtype", &OutputVStreamWrapper::get_dtype)
    .def_property_readonly("shape", &OutputVStreamWrapper::get_shape)
    .def("get_user_buffer_format", &OutputVStreamWrapper::get_user_buffer_format)
    ;
}

OutputVStreamsWrapper OutputVStreamsWrapper::create(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &output_vstreams_params)
{
    auto output_vstreams_expected = VStreamsBuilder::create_output_vstreams(net_group, output_vstreams_params);
    VALIDATE_STATUS(output_vstreams_expected.status());

    std::unordered_map<std::string, std::shared_ptr<OutputVStream>> output_vstreams;
    for (auto &output : output_vstreams_expected.value()) {
        output_vstreams.emplace(output.name(), make_shared_nothrow<OutputVStream>(std::move(output)));
    }
    return OutputVStreamsWrapper(output_vstreams);
}

std::shared_ptr<OutputVStream> OutputVStreamsWrapper::get_output_by_name(const std::string &name)
{
    auto output = m_output_vstreams.find(name);
    if (m_output_vstreams.end() == output) {
        LOGGER__ERROR("Output virtual stream for name={} not found", name);
        THROW_STATUS_ERROR(HAILO_NOT_FOUND);
    }

    return output->second;
}

const OutputVStreamsWrapper &OutputVStreamsWrapper::enter()
{
    return std::ref(*this);
}

void OutputVStreamsWrapper::exit()
{
    m_output_vstreams.clear();
}

py::dict OutputVStreamsWrapper::get_all_outputs()
{
    return py::cast(m_output_vstreams);
}

void OutputVStreamsWrapper::clear()
{
    std::vector<std::reference_wrapper<OutputVStream>> outputs;
    outputs.reserve(m_output_vstreams.size());
    for (auto &name_vstream_pair : m_output_vstreams) {
        outputs.emplace_back(std::ref(*name_vstream_pair.second));
    }
    
    auto status = OutputVStream::clear(outputs);
    VALIDATE_STATUS(status);
}

void OutputVStreamsWrapper::add_to_python_module(py::module &m)
{
    py::class_<OutputVStreamsWrapper>(m, "OutputVStreams")
    .def(py::init(&OutputVStreamsWrapper::create))
    .def("get_output_by_name", &OutputVStreamsWrapper::get_output_by_name)
    .def("get_all_outputs", &OutputVStreamsWrapper::get_all_outputs)
    .def("clear", &OutputVStreamsWrapper::clear)
    .def("__enter__", &OutputVStreamsWrapper::enter, py::return_value_policy::reference)
    .def("__exit__",  [&](OutputVStreamsWrapper &self, py::args) { self.exit(); })
    ;
}

OutputVStreamsWrapper::OutputVStreamsWrapper(std::unordered_map<std::string, std::shared_ptr<OutputVStream>> &output_vstreams)
    : m_output_vstreams(std::move(output_vstreams))
{}

InferVStreamsWrapper InferVStreamsWrapper::create(ConfiguredNetworkGroup &network_group,
    const std::map<std::string, hailo_vstream_params_t> &input_vstreams_params,
    const std::map<std::string, hailo_vstream_params_t> &output_vstreams_params)
{
    auto infer_pipeline = InferVStreams::create(network_group, input_vstreams_params, output_vstreams_params);
    VALIDATE_EXPECTED(infer_pipeline);
    auto infer_vstream_ptr = make_shared_nothrow<InferVStreams>(std::move(infer_pipeline.value()));

    return InferVStreamsWrapper(infer_vstream_ptr);
}

void InferVStreamsWrapper::infer(std::map<std::string, py::array> input_data, std::map<std::string, py::array> output_data,
    size_t batch_size)
{
    std::map<std::string, MemoryView> input_data_c;
    std::map<std::string, MemoryView> output_data_c;

    for (auto& name_pair : input_data) {
        input_data_c.emplace(name_pair.first, MemoryView(name_pair.second.mutable_data(),
            static_cast<size_t>(name_pair.second.nbytes())));
    }

    for (auto& name_pair : output_data) {
        output_data_c.emplace(name_pair.first, MemoryView(name_pair.second.mutable_data(),
            static_cast<size_t>(name_pair.second.nbytes())));
    }

    hailo_status status = m_infer_pipeline->infer(input_data_c, output_data_c, batch_size);
    VALIDATE_STATUS(status);
}

py::dtype InferVStreamsWrapper::get_host_dtype(const std::string &stream_name)
{
    auto input = m_infer_pipeline->get_input_by_name(stream_name);
    if (HAILO_SUCCESS == input.status()) {
        return HailoRTBindingsCommon::get_dtype(input->get().get_user_buffer_format().type);
    } else if (HAILO_NOT_FOUND != input.status()) {
        THROW_STATUS_ERROR(input.status());
    }
    auto output = m_infer_pipeline->get_output_by_name(stream_name);
    VALIDATE_EXPECTED(output);

    return HailoRTBindingsCommon::get_dtype(output->get().get_user_buffer_format().type);
}

hailo_format_t InferVStreamsWrapper::get_user_buffer_format(const std::string &stream_name)
{
    auto input = m_infer_pipeline->get_input_by_name(stream_name);
    if (HAILO_SUCCESS == input.status()) {
        return input->get().get_user_buffer_format();
    } else if (HAILO_NOT_FOUND != input.status()) {
        THROW_STATUS_ERROR(input.status());
    }
    auto output = m_infer_pipeline->get_output_by_name(stream_name);
    VALIDATE_EXPECTED(output);

    return output->get().get_user_buffer_format();
}

std::vector<size_t> InferVStreamsWrapper::get_shape(const std::string &stream_name)
{
    auto input = m_infer_pipeline->get_input_by_name(stream_name);
    if (HAILO_SUCCESS == input.status()) {
        return HailoRTBindingsCommon::get_pybind_shape(input->get().get_info(), input->get().get_user_buffer_format());
    }

    auto output = m_infer_pipeline->get_output_by_name(stream_name);
    if (HAILO_SUCCESS == output.status()) {
        return HailoRTBindingsCommon::get_pybind_shape(output->get().get_info(), output->get().get_user_buffer_format());
    }

    LOGGER__ERROR("Stream {} not found", stream_name);
    THROW_STATUS_ERROR(HAILO_NOT_FOUND);
}

void InferVStreamsWrapper::release()
{
    m_infer_pipeline.reset();
}

void InferVStreamsWrapper::add_to_python_module(py::module &m)
{
    py::class_<InferVStreamsWrapper>(m, "InferVStreams")
    .def(py::init(&InferVStreamsWrapper::create))
    .def("get_host_dtype", &InferVStreamsWrapper::get_host_dtype)
    .def("get_shape", &InferVStreamsWrapper::get_shape)
    .def("get_user_buffer_format", &InferVStreamsWrapper::get_user_buffer_format)
    .def("infer", &InferVStreamsWrapper::infer)
    .def("release",  [](InferVStreamsWrapper &self, py::args) { self.release(); })
    ;
}

InferVStreamsWrapper::InferVStreamsWrapper(std::shared_ptr<InferVStreams> &infer_pipeline)
    : m_infer_pipeline(std::move(infer_pipeline))
{}

void VStream_api_initialize_python_module(py::module &m)
{
    InputVStreamWrapper::add_to_python_module(m);
    InputVStreamsWrapper::add_to_python_module(m);
    OutputVStreamWrapper::add_to_python_module(m);
    OutputVStreamsWrapper::add_to_python_module(m);
    InferVStreamsWrapper::add_to_python_module(m);
}

} /* namespace hailort */
