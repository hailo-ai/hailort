/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream_api.cpp
 * @brief Implementation of binding to virtual stream usage over Python.
 **/

#include "vstream_api.hpp"
#include "bindings_common.hpp"
#include "utils.hpp"
#include "network_group_api.hpp"

#include <iostream>


namespace hailort
{

void InputVStreamWrapper::bind(py::module &m)
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
        auto shape = self.get_info().shape;
        auto nms_shape = self.get_info().nms_shape;
        auto format = self.get_user_buffer_format();
        return *py::array::ShapeContainer(HailoRTBindingsCommon::get_pybind_shape(shape, nms_shape, format));
    })
    ;
}

InputVStreamsWrapperPtr InputVStreamsWrapper::create(ConfiguredNetworkGroup &net_group,
    const std::map<std::string, hailo_vstream_params_t> &input_vstreams_params)
{
    auto input_vstreams_expected = VStreamsBuilder::create_input_vstreams(net_group, input_vstreams_params);
    VALIDATE_STATUS(input_vstreams_expected.status());

    std::unordered_map<std::string, std::shared_ptr<InputVStream>> input_vstreams;
    for (auto &input : input_vstreams_expected.value()) {
        auto input_name = input.name();
        input_vstreams.emplace(input_name, std::make_unique<InputVStream>(std::move(input)));
    }
    return std::make_shared<InputVStreamsWrapper>(input_vstreams);
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
        std::cerr << "Input virtual stream for name=" << name << " not found";
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

void InputVStreamsWrapper::bind(py::module &m)
{
    py::class_<InputVStreamsWrapper, InputVStreamsWrapperPtr>(m, "InputVStreams")
    .def(py::init(&InputVStreamsWrapper::create))
    .def("get_input_by_name", &InputVStreamsWrapper::get_input_by_name)
    .def("get_all_inputs", &InputVStreamsWrapper::get_all_inputs)
    .def("clear", &InputVStreamsWrapper::clear)
    .def("__enter__", &InputVStreamsWrapper::enter, py::return_value_policy::reference)
    .def("__exit__",  [&](InputVStreamsWrapper &self, py::args) { self.exit(); })
    ;
}

InputVStreamsWrapper::InputVStreamsWrapper(std::unordered_map<std::string, std::shared_ptr<InputVStream>> &input_vstreams) :
    m_input_vstreams(std::move(input_vstreams))
#ifdef HAILO_IS_FORK_SUPPORTED
        ,
        m_atfork_guard(this, {
            .before_fork = [this]() { before_fork(); },
            .after_fork_in_parent = [this]() { after_fork_in_parent(); },
            .after_fork_in_child = [this]() { after_fork_in_child(); }
        })
#endif
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
    auto shape = self.get_info().shape;
    auto nms_shape = self.get_info().nms_shape;
    auto format = self.get_user_buffer_format();
    return *py::array::ShapeContainer(HailoRTBindingsCommon::get_pybind_shape(shape, nms_shape, format));
}

void OutputVStreamWrapper::bind(py::module &m)
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
        auto unmanaged_addr_exp = buffer->release();
        VALIDATE_EXPECTED(unmanaged_addr_exp);
        const auto unmanaged_addr = unmanaged_addr_exp.release();
        return py::array(get_dtype(self), get_shape(self), unmanaged_addr,
            py::capsule(unmanaged_addr, [](void *p) { delete reinterpret_cast<uint8_t*>(p); }));
    })
    .def("set_nms_score_threshold", [](OutputVStream &self, float32_t threshold)
    {
        hailo_status status = self.set_nms_score_threshold(threshold);
        VALIDATE_STATUS(status);
    })
    .def("set_nms_iou_threshold", [](OutputVStream &self, float32_t threshold)
    {
        hailo_status status = self.set_nms_iou_threshold(threshold);
        VALIDATE_STATUS(status);
    })
    .def("set_nms_max_proposals_per_class", [](OutputVStream &self, uint32_t max_proposals_per_class)
    {
        hailo_status status = self.set_nms_max_proposals_per_class(max_proposals_per_class);
        VALIDATE_STATUS(status);
    })
    .def("set_nms_max_accumulated_mask_size", [](OutputVStream &self, uint32_t max_accumulated_mask_size)
    {
        hailo_status status = self.set_nms_max_accumulated_mask_size(max_accumulated_mask_size);
        VALIDATE_STATUS(status);
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

OutputVStreamsWrapperPtr OutputVStreamsWrapper::create(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &output_vstreams_params)
{
    auto output_vstreams_expected = VStreamsBuilder::create_output_vstreams(net_group, output_vstreams_params);
    VALIDATE_STATUS(output_vstreams_expected.status());

    std::unordered_map<std::string, std::shared_ptr<OutputVStream>> output_vstreams;
    for (auto &output : output_vstreams_expected.value()) {
        auto output_name = output.name();
        output_vstreams.emplace(output_name, std::make_unique<OutputVStream>(std::move(output)));
    }
    return std::make_shared<OutputVStreamsWrapper>(output_vstreams);
}

std::shared_ptr<OutputVStream> OutputVStreamsWrapper::get_output_by_name(const std::string &name)
{
    auto output = m_output_vstreams.find(name);
    if (m_output_vstreams.end() == output) {
        std::cerr << "Output virtual stream for name=" << name << " not found";
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

void OutputVStreamsWrapper::before_fork()
{
    for (auto &vstream : m_output_vstreams) {
        vstream.second->before_fork();
    }
}

void OutputVStreamsWrapper::after_fork_in_parent()
{
    for (auto &vstream : m_output_vstreams) {
        vstream.second->after_fork_in_parent();
    }
}

void OutputVStreamsWrapper::after_fork_in_child()
{
    for (auto &vstream : m_output_vstreams) {
        vstream.second->after_fork_in_child();
    }
}

void OutputVStreamsWrapper::bind(py::module &m)
{
    py::class_<OutputVStreamsWrapper, OutputVStreamsWrapperPtr>(m, "OutputVStreams")
    .def(py::init(&OutputVStreamsWrapper::create))
    .def("get_output_by_name", &OutputVStreamsWrapper::get_output_by_name)
    .def("get_all_outputs", &OutputVStreamsWrapper::get_all_outputs)
    .def("clear", &OutputVStreamsWrapper::clear)
    .def("__enter__", &OutputVStreamsWrapper::enter, py::return_value_policy::reference)
    .def("__exit__",  [&](OutputVStreamsWrapper &self, py::args) { self.exit(); })
    ;
}

OutputVStreamsWrapper::OutputVStreamsWrapper(std::unordered_map<std::string, std::shared_ptr<OutputVStream>> &output_vstreams) :
    m_output_vstreams(std::move(output_vstreams))
#ifdef HAILO_IS_FORK_SUPPORTED
        ,
        m_atfork_guard(this, {
            .before_fork = [this]() { before_fork(); },
            .after_fork_in_parent = [this]() { after_fork_in_parent(); },
            .after_fork_in_child = [this]() { after_fork_in_child(); }
        })
#endif
{}

InferVStreamsWrapper InferVStreamsWrapper::create(ConfiguredNetworkGroupWrapper &network_group,
    const std::map<std::string, hailo_vstream_params_t> &input_vstreams_params,
    const std::map<std::string, hailo_vstream_params_t> &output_vstreams_params)
{
    auto infer_pipeline = InferVStreams::create(network_group.get(), input_vstreams_params, output_vstreams_params);
    VALIDATE_EXPECTED(infer_pipeline);
    auto infer_vstream_ptr = std::make_shared<InferVStreams>(std::move(infer_pipeline.value()));

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
        auto shape = input->get().get_info().shape;
        auto nms_shape = input->get().get_info().nms_shape;
        auto format = input->get().get_user_buffer_format();
        return HailoRTBindingsCommon::get_pybind_shape(shape, nms_shape, format);
    }

    auto output = m_infer_pipeline->get_output_by_name(stream_name);
    if (HAILO_SUCCESS == output.status()) {
        auto shape = output->get().get_info().shape;
        auto nms_shape = output->get().get_info().nms_shape;
        auto format = output->get().get_user_buffer_format();
        return HailoRTBindingsCommon::get_pybind_shape(shape, nms_shape, format);
    }

    std::cerr << "Stream " << stream_name << " not found";
    THROW_STATUS_ERROR(HAILO_NOT_FOUND);
}

void InferVStreamsWrapper::release()
{
    m_infer_pipeline.reset();
}

void InputVStreamsWrapper::before_fork()
{
    for (auto &vstream : m_input_vstreams) {
        vstream.second->before_fork();
    }
}
void InputVStreamsWrapper::after_fork_in_parent()
{
    for (auto &vstream : m_input_vstreams) {
        vstream.second->after_fork_in_parent();
    }
}
void InputVStreamsWrapper::after_fork_in_child()
{
    for (auto &vstream : m_input_vstreams) {
        vstream.second->after_fork_in_child();
    }
}

void InferVStreamsWrapper::bind(py::module &m)
{
    py::class_<InferVStreamsWrapper>(m, "InferVStreams")
    .def(py::init(&InferVStreamsWrapper::create))
    .def("get_host_dtype", &InferVStreamsWrapper::get_host_dtype)
    .def("get_shape", &InferVStreamsWrapper::get_shape)
    .def("get_user_buffer_format", &InferVStreamsWrapper::get_user_buffer_format)
    .def("infer", &InferVStreamsWrapper::infer)
    .def("release",  [](InferVStreamsWrapper &self, py::args) { self.release(); })
    .def("set_nms_score_threshold", [](InferVStreamsWrapper &self, float32_t threshold)
    {
        VALIDATE_STATUS(self.m_infer_pipeline->set_nms_score_threshold(threshold));
    })
    .def("set_nms_iou_threshold", [](InferVStreamsWrapper &self, float32_t threshold)
    {
        VALIDATE_STATUS(self.m_infer_pipeline->set_nms_iou_threshold(threshold));
    })
    .def("set_nms_max_proposals_per_class", [](InferVStreamsWrapper &self, uint32_t max_proposals_per_class)
    {
        VALIDATE_STATUS(self.m_infer_pipeline->set_nms_max_proposals_per_class(max_proposals_per_class));
    })
    .def("set_nms_max_accumulated_mask_size", [](InferVStreamsWrapper &self, uint32_t max_accumulated_mask_size)
    {
        VALIDATE_STATUS(self.m_infer_pipeline->set_nms_max_accumulated_mask_size(max_accumulated_mask_size));
    })
    ;
}

InferVStreamsWrapper::InferVStreamsWrapper(std::shared_ptr<InferVStreams> &infer_pipeline)
    : m_infer_pipeline(std::move(infer_pipeline))
{}

} /* namespace hailort */
