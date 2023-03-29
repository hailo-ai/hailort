/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hef_api.cpp
 * @brief implementation of binding to an HEF class, and network_group usage over Python.
 *
 * TODO: doc
 **/

#include "hef_api.hpp"


namespace hailort
{

HefWrapper::HefWrapper(const std::string &hef_path)
{
    auto hef_expected = Hef::create(hef_path);
    VALIDATE_EXPECTED(hef_expected);

    hef = make_unique_nothrow<Hef>(hef_expected.release());
    if (nullptr == hef) {
        THROW_STATUS_ERROR(HAILO_OUT_OF_HOST_MEMORY);
    }
}

HefWrapper::HefWrapper(const MemoryView &hef_buffer)
{
    auto hef_expected = Hef::create(hef_buffer);
    VALIDATE_EXPECTED(hef_expected);

    hef = make_unique_nothrow<Hef>(hef_expected.release());
    if (nullptr == hef) {
        THROW_STATUS_ERROR(HAILO_OUT_OF_HOST_MEMORY);
    }
}

HefWrapper HefWrapper::create_from_buffer(py::bytes data)
{
    return HefWrapper(MemoryView((uint8_t*)std::string(data).c_str(), std::string(data).size()));
}

HefWrapper HefWrapper::create_from_file(const std::string &hef_path)
{
    return HefWrapper(hef_path);
}

py::list HefWrapper::get_network_group_names()
{
    return py::cast(hef->get_network_groups_names());
}

py::list HefWrapper::get_network_groups_infos()
{
    auto network_group_infos = hef->get_network_groups_infos();
    VALIDATE_EXPECTED(network_group_infos);
    return py::cast(network_group_infos.release());
}

py::list HefWrapper::get_sorted_output_names(std::string net_group_name)
{
    auto names_list = hef->get_sorted_output_names(net_group_name);
    VALIDATE_EXPECTED(names_list);

    return py::cast(names_list.release());
}

float64_t HefWrapper::get_bottleneck_fps(const std::string &net_group_name)
{
    Expected<float64_t> bottleneck_fps = hef->get_bottleneck_fps(net_group_name);
    VALIDATE_EXPECTED(bottleneck_fps);
    return bottleneck_fps.release();
}

py::dict HefWrapper::get_udp_rates_dict(const std::string &net_group_name, uint32_t fps, uint32_t max_supported_rate_bytes)
{
    auto rate_calculator = NetworkUdpRateCalculator::create(hef.release(), net_group_name);
    VALIDATE_EXPECTED(rate_calculator);
    auto rates_per_name = rate_calculator.value().calculate_inputs_bandwith(fps, max_supported_rate_bytes);
    VALIDATE_EXPECTED(rates_per_name);
    return py::cast(rates_per_name.release());
}

py::list HefWrapper::get_original_names_from_vstream_name(const std::string &vstream_name, const std::string &net_group_name)
{
    auto results = hef->get_original_names_from_vstream_name(vstream_name, net_group_name);
    VALIDATE_EXPECTED(results);
    return py::cast(results.release());
}

std::string HefWrapper::get_vstream_name_from_original_name(const std::string &original_name, const std::string &net_group_name)
{
    auto results = hef->get_vstream_name_from_original_name(original_name, net_group_name);
    VALIDATE_EXPECTED(results);
    return results.release();
}

py::list HefWrapper::get_stream_names_from_vstream_name(const std::string &vstream_name, const std::string &net_group_name)
{
    auto results = hef->get_stream_names_from_vstream_name(vstream_name, net_group_name);
    VALIDATE_EXPECTED(results);
    return py::cast(results.release());
}

py::list HefWrapper::get_vstream_names_from_stream_name(const std::string &stream_name, const std::string &net_group_name)
{
    auto results = hef->get_vstream_names_from_stream_name(stream_name, net_group_name);
    VALIDATE_EXPECTED(results);
    return py::cast(results.release());
}

py::dict HefWrapper::get_input_vstreams_params(const std::string &name, bool quantized, hailo_format_type_t format_type,
    uint32_t timeout_ms, uint32_t queue_size)
{
    auto result = hef->make_input_vstream_params(name, quantized, format_type, timeout_ms, queue_size);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::dict HefWrapper::get_output_vstreams_params(const std::string &name, bool quantized, hailo_format_type_t format_type,
    uint32_t timeout_ms, uint32_t queue_size)
{
    auto result = hef->make_output_vstream_params(name, quantized, format_type, timeout_ms, queue_size);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::list HefWrapper::get_input_vstream_infos(const std::string &name)
{
    auto result = hef->get_input_vstream_infos(name);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::list HefWrapper::get_output_vstream_infos(const std::string &name)
{
    auto result = hef->get_output_vstream_infos(name);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::list HefWrapper::get_all_vstream_infos(const std::string &name)
{
    auto result = hef->get_all_vstream_infos(name);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::list HefWrapper::get_input_stream_infos(const std::string &name)
{
    auto result = hef->get_input_stream_infos(name);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::list HefWrapper::get_output_stream_infos(const std::string &name)
{
    auto result = hef->get_output_stream_infos(name);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::list HefWrapper::get_all_stream_infos(const std::string &name)
{
    auto result = hef->get_all_stream_infos(name);
    VALIDATE_EXPECTED(result);
    return py::cast(result.value());
}

py::dict HefWrapper::create_configure_params(hailo_stream_interface_t interface)
{
    auto configure_params = hef->create_configure_params(interface);
    VALIDATE_EXPECTED(configure_params);

    return py::cast(configure_params.release());
}

py::dict HefWrapper::create_configure_params_mipi_input(hailo_stream_interface_t output_interface,
    const hailo_mipi_input_stream_params_t &mipi_params)
{
    auto configure_params = hef->create_configure_params_mipi_input(output_interface, mipi_params);
    VALIDATE_EXPECTED(configure_params);

    return py::cast(configure_params.release());
}

py::list HefWrapper::get_networks_names(const std::string &net_group_name)
{
    auto network_infos = hef->get_network_infos(net_group_name);
    VALIDATE_EXPECTED(network_infos);

    std::vector<std::string> res;
    for (const auto &info : network_infos.value()) {
        res.push_back(info.name);
    }

    return py::cast(res);
}

ActivatedAppContextManagerWrapper::ActivatedAppContextManagerWrapper(ConfiguredNetworkGroup &net_group,
    const hailo_activate_network_group_params_t &network_group_params) :
        m_net_group(net_group), m_network_group_params(network_group_params)
    {}

const ActivatedNetworkGroup& ActivatedAppContextManagerWrapper::enter()
{
    auto activated = m_net_group.activate(m_network_group_params);
    if (activated.status() != HAILO_NOT_IMPLEMENTED) {
        VALIDATE_EXPECTED(activated);
        m_activated_net_group = activated.release();
    }

    return std::ref(*m_activated_net_group);
}

void ActivatedAppContextManagerWrapper::exit()
{
    m_activated_net_group.reset();
}

void ActivatedAppContextManagerWrapper::add_to_python_module(py::module &m)
{
    py::class_<ActivatedAppContextManagerWrapper>(m, "ActivatedApp")
    .def("__enter__", &ActivatedAppContextManagerWrapper::enter, py::return_value_policy::reference)
    .def("__exit__",  [&](ActivatedAppContextManagerWrapper &self, py::args) { self.exit(); })
    ;
}

void HefWrapper::initialize_python_module(py::module &m)
{
    py::class_<HefWrapper>(m, "Hef")
        .def("create_from_buffer", &HefWrapper::create_from_buffer)
        .def("create_from_file", &HefWrapper::create_from_file)
        .def("get_network_group_names", &HefWrapper::get_network_group_names)
        .def("get_network_groups_infos", &HefWrapper::get_network_groups_infos)
        .def("get_sorted_output_names", &HefWrapper::get_sorted_output_names)
        .def("get_bottleneck_fps", &HefWrapper::get_bottleneck_fps)
        .def("get_stream_names_from_vstream_name", &HefWrapper::get_stream_names_from_vstream_name)
        .def("get_vstream_names_from_stream_name", &HefWrapper::get_vstream_names_from_stream_name)
        .def("get_vstream_name_from_original_name", &HefWrapper::get_vstream_name_from_original_name)
        .def("get_original_names_from_vstream_name", &HefWrapper::get_original_names_from_vstream_name)
        .def("get_udp_rates_dict", &HefWrapper::get_udp_rates_dict)
        .def("create_configure_params", &HefWrapper::create_configure_params)
        .def("create_configure_params_mipi_input", &HefWrapper::create_configure_params_mipi_input)
        .def("get_input_vstreams_params", &HefWrapper::get_input_vstreams_params)
        .def("get_output_vstreams_params", &HefWrapper::get_output_vstreams_params)
        .def("get_input_vstream_infos", &HefWrapper::get_input_vstream_infos)
        .def("get_output_vstream_infos", &HefWrapper::get_output_vstream_infos)
        .def("get_all_vstream_infos", &HefWrapper::get_all_vstream_infos)
        .def("get_input_stream_infos", &HefWrapper::get_input_stream_infos)
        .def("get_output_stream_infos", &HefWrapper::get_output_stream_infos)
        .def("get_all_stream_infos", &HefWrapper::get_all_stream_infos)
        .def("get_networks_names", &HefWrapper::get_networks_names)
        ;

    py::class_<ConfiguredNetworkGroup>(m, "ConfiguredNetworkGroup")
        .def("get_name", [](ConfiguredNetworkGroup& self)
            {
                return self.name();
            })
        .def("get_default_streams_interface", [](ConfiguredNetworkGroup& self)
            {
                auto result = self.get_default_streams_interface();
                VALIDATE_EXPECTED(result);
                return result.value();
            })
        .def("activate", [](ConfiguredNetworkGroup& self,
            const hailo_activate_network_group_params_t &network_group_params)
            {
                return ActivatedAppContextManagerWrapper(self, network_group_params);
            })
        .def("wait_for_activation", [](ConfiguredNetworkGroup& self, uint32_t timeout_ms)
            {
                auto status = self.wait_for_activation(std::chrono::milliseconds(timeout_ms));
                if (status != HAILO_NOT_IMPLEMENTED) {
                    VALIDATE_STATUS(status);
                }
            })
        .def("InputVStreams", [](ConfiguredNetworkGroup &self, std::map<std::string, hailo_vstream_params_t> &input_vstreams_params)
            {
                return InputVStreamsWrapper::create(self, input_vstreams_params);
            })
        .def("OutputVStreams", [](ConfiguredNetworkGroup &self, std::map<std::string, hailo_vstream_params_t> &output_vstreams_params)
            {
                return OutputVStreamsWrapper::create(self, output_vstreams_params);
            })
        .def("get_udp_rates_dict", [](ConfiguredNetworkGroup& self, uint32_t fps, uint32_t max_supported_rate_bytes)
        {
            auto rate_calculator = NetworkUdpRateCalculator::create(self);
            VALIDATE_EXPECTED(rate_calculator);

            auto udp_input_streams = self.get_input_streams_by_interface(HAILO_STREAM_INTERFACE_ETH);
            auto results = rate_calculator->get_udp_ports_rates_dict(udp_input_streams,
                fps, max_supported_rate_bytes);
            VALIDATE_EXPECTED(results);

            return py::cast(results.value());
        })
        .def("before_fork", [](ConfiguredNetworkGroup& self)
        {
#ifdef HAILO_SUPPORT_MULTI_PROCESS
            auto status = self.before_fork();
            VALIDATE_STATUS(status);
#else
            (void)self;
#endif // HAILO_SUPPORT_MULTI_PROCESS
        })
        .def("after_fork_in_parent", [](ConfiguredNetworkGroup& self)
        {
#ifdef HAILO_SUPPORT_MULTI_PROCESS
            auto status = self.after_fork_in_parent();
            VALIDATE_STATUS(status);            
#else
            (void)self;
#endif // HAILO_SUPPORT_MULTI_PROCESS
        })
        .def("after_fork_in_child", [](ConfiguredNetworkGroup& self)
        {
#ifdef HAILO_SUPPORT_MULTI_PROCESS
            auto status = self.after_fork_in_child();
            VALIDATE_STATUS(status);
#else
            (void)self;
#endif // HAILO_SUPPORT_MULTI_PROCESS
        })
        .def("set_scheduler_timeout", [](ConfiguredNetworkGroup& self, int timeout, const std::string &network_name="")
        {
            auto timeout_mili = std::chrono::milliseconds(timeout);
            auto status = self.set_scheduler_timeout(timeout_mili, network_name);
            VALIDATE_STATUS(status);
        })
        .def("set_scheduler_threshold", [](ConfiguredNetworkGroup& self, uint32_t threshold)
        {
            auto status = self.set_scheduler_threshold(threshold);
            VALIDATE_STATUS(status);
        })
        .def("set_scheduler_priority", [](ConfiguredNetworkGroup& self, uint8_t priority)
        {
            auto status = self.set_scheduler_priority(priority);
            VALIDATE_STATUS(status);
        })
        ;

    ActivatedAppContextManagerWrapper::add_to_python_module(m);

    py::class_<ActivatedNetworkGroup>(m, "ActivatedNetworkGroup")
        .def("get_intermediate_buffer", [](ActivatedNetworkGroup& self, uint8_t src_context_index,
            uint8_t src_stream_index)
        {
            auto buff = self.get_intermediate_buffer(std::make_pair(src_context_index, src_stream_index));
            VALIDATE_EXPECTED(buff);

            return py::bytes(reinterpret_cast<char*>(buff->data()), buff->size());
        })
        .def("get_invalid_frames_count", [](ActivatedNetworkGroup& self)
        {
            return self.get_invalid_frames_count();
        })
        ;
}

} /* namespace hailort */
