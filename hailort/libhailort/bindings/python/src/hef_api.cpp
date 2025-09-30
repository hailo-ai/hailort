/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hef_api.cpp
 * @brief implementation of binding to an HEF class, and network_group usage over Python.
 *
 * TODO: doc
 **/

#include "hef_api.hpp"
#include <memory>

namespace hailort
{

HefWrapper::HefWrapper(const std::string &hef_path)
{
    auto hef_expected = Hef::create(hef_path);
    VALIDATE_EXPECTED(hef_expected);

    hef = std::make_unique<Hef>(hef_expected.release());
    if (nullptr == hef) {
        THROW_STATUS_ERROR(HAILO_OUT_OF_HOST_MEMORY);
    }
}

HefWrapper::HefWrapper(const MemoryView &hef_buffer)
{
    auto hef_expected = Hef::create(hef_buffer);
    VALIDATE_EXPECTED(hef_expected);

    hef = std::make_unique<Hef>(hef_expected.release());
    if (nullptr == hef) {
        THROW_STATUS_ERROR(HAILO_OUT_OF_HOST_MEMORY);
    }
}

HefWrapper HefWrapper::create_from_buffer(const py::bytes &data)
{
    // TODO: HRT-13713 - When adding support to read the hef from pre-allocated memory,
    //  we will need to make sure the hef memory is not released here.
    py::buffer_info info(py::buffer(data).request());
    return HefWrapper(MemoryView::create_const(info.ptr, info.size));
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

py::bytes HefWrapper::get_external_resources(const std::string &resource_name)
{
    auto external_resource = hef->get_external_resources(resource_name);
    VALIDATE_EXPECTED(external_resource);
    return py::bytes(external_resource.value().as_pointer<char>(), external_resource.value().size());
}

py::list HefWrapper::get_external_resource_names()
{
    auto resource_names = hef->get_external_resource_names();
    return py::cast(resource_names);
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

void HefWrapper::bind(py::module &m)
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
        .def("create_configure_params", &HefWrapper::create_configure_params)
        .def("get_input_vstream_infos", &HefWrapper::get_input_vstream_infos)
        .def("get_output_vstream_infos", &HefWrapper::get_output_vstream_infos)
        .def("get_all_vstream_infos", &HefWrapper::get_all_vstream_infos)
        .def("get_input_stream_infos", &HefWrapper::get_input_stream_infos)
        .def("get_output_stream_infos", &HefWrapper::get_output_stream_infos)
        .def("get_all_stream_infos", &HefWrapper::get_all_stream_infos)
        .def("get_networks_names", &HefWrapper::get_networks_names)
        .def("get_external_resources", &HefWrapper::get_external_resources)
        .def("get_external_resource_names", &HefWrapper::get_external_resource_names)
        ;
}

} /* namespace hailort */
