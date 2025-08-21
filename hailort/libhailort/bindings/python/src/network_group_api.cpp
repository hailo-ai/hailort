/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_api.cpp
 **/

#include "network_group_api.hpp"


namespace hailort
{

void ConfiguredNetworkGroupWrapper::bind(py::module &m)
{
    py::class_<ConfiguredNetworkGroupWrapper, ConfiguredNetworkGroupWrapperPtr>(m, "ConfiguredNetworkGroup")
        .def("is_scheduled", &ConfiguredNetworkGroupWrapper::is_scheduled)
        .def("get_name", &ConfiguredNetworkGroupWrapper::get_name)
        .def("get_default_streams_interface", &ConfiguredNetworkGroupWrapper::get_default_streams_interface)
        .def("activate", &ConfiguredNetworkGroupWrapper::activate)
        .def("wait_for_activation", &ConfiguredNetworkGroupWrapper::wait_for_activation)
        .def("InputVStreams", &ConfiguredNetworkGroupWrapper::InputVStreams)
        .def("OutputVStreams", &ConfiguredNetworkGroupWrapper::OutputVStreams)
        .def("set_scheduler_timeout", &ConfiguredNetworkGroupWrapper::set_scheduler_timeout)
        .def("set_scheduler_threshold", &ConfiguredNetworkGroupWrapper::set_scheduler_threshold)
        .def("set_scheduler_priority", &ConfiguredNetworkGroupWrapper::set_scheduler_priority)
        .def("init_cache", &ConfiguredNetworkGroupWrapper::init_cache)
        .def("update_cache_offset", &ConfiguredNetworkGroupWrapper::update_cache_offset)
        .def("get_cache_ids", &ConfiguredNetworkGroupWrapper::get_cache_ids)
        .def("read_cache_buffer", &ConfiguredNetworkGroupWrapper::read_cache_buffer)
        .def("write_cache_buffer", &ConfiguredNetworkGroupWrapper::write_cache_buffer)
        .def("get_networks_names", &ConfiguredNetworkGroupWrapper::get_networks_names)
        .def("get_sorted_output_names", &ConfiguredNetworkGroupWrapper::get_sorted_output_names)
        .def("get_input_vstream_infos", &ConfiguredNetworkGroupWrapper::get_input_vstream_infos)
        .def("get_output_vstream_infos", &ConfiguredNetworkGroupWrapper::get_output_vstream_infos)
        .def("get_all_vstream_infos", &ConfiguredNetworkGroupWrapper::get_all_vstream_infos)
        .def("get_all_stream_infos", &ConfiguredNetworkGroupWrapper::get_all_stream_infos)
        .def("get_input_stream_infos", &ConfiguredNetworkGroupWrapper::get_input_stream_infos)
        .def("get_output_stream_infos", &ConfiguredNetworkGroupWrapper::get_output_stream_infos)
        .def("get_vstream_names_from_stream_name", &ConfiguredNetworkGroupWrapper::get_vstream_names_from_stream_name)
        .def("get_stream_names_from_vstream_name", &ConfiguredNetworkGroupWrapper::get_stream_names_from_vstream_name)
        .def("make_input_vstream_params", &ConfiguredNetworkGroupWrapper::make_input_vstream_params)
        .def("make_output_vstream_params", &ConfiguredNetworkGroupWrapper::make_output_vstream_params)
        ;
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

void ActivatedAppContextManagerWrapper::bind(py::module &m)
{
    py::class_<ActivatedAppContextManagerWrapper>(m, "ActivatedApp")
    .def("__enter__", &ActivatedAppContextManagerWrapper::enter, py::return_value_policy::reference)
    .def("__exit__",  [&](ActivatedAppContextManagerWrapper &self, py::args) { self.exit(); })
    ;

    py::class_<ActivatedNetworkGroup>(m, "ActivatedNetworkGroup")
        .def("get_intermediate_buffer", [](ActivatedNetworkGroup& self, uint16_t src_context_index,
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
