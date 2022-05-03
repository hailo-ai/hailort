/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hef_api.hpp
 * @brief Defines binding to an HEF class, and network_group usage over Python.
 *
 * TODO: doc
 **/

#ifndef HEF_API_HPP_
#define HEF_API_HPP_

#include "hailo/hef.hpp"
#include "hailo/network_rate_calculator.hpp"
#include "hailo/network_group.hpp"

#include "vstream_api.hpp"
#include "utils.hpp"
#include "common/logger_macros.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>

#include <string>

namespace hailort
{

class HefWrapper {
public:
    HefWrapper(const std::string &hef_path);
    HefWrapper(const MemoryView &hef_buffer);
    static HefWrapper create_from_buffer(py::bytes data);
    static HefWrapper create_from_file(const std::string &hef_path);
    py::list get_network_group_names();
    py::list get_network_groups_infos();
    py::list get_sorted_output_names(std::string net_group_name);
    float64_t get_bottleneck_fps(const std::string &net_group_name);
    py::dict get_udp_rates_dict(const std::string &net_group_name, uint32_t fps, uint32_t max_supported_rate_bytes);
    py::list get_original_names_from_vstream_name(const std::string &vstream_name, const std::string &net_group_name);
    std::string get_vstream_name_from_original_name(const std::string &original_name, const std::string &net_group_name);
    py::list get_stream_names_from_vstream_name(const std::string &vstream_name, const std::string &net_group_name);
    py::list get_vstream_names_from_stream_name(const std::string &stream_name, const std::string &net_group_name);
    py::dict get_input_vstreams_params(const std::string &name, bool quantized, hailo_format_type_t format_type,
        uint32_t timeout_ms, uint32_t queue_size);
    py::dict get_output_vstreams_params(const std::string &name, bool quantized, hailo_format_type_t format_type,
        uint32_t timeout_ms, uint32_t queue_size);
    py::list get_input_vstream_infos(const std::string &name);
    py::list get_output_vstream_infos(const std::string &name);
    py::list get_all_vstream_infos(const std::string &name);
    py::list get_input_stream_infos(const std::string &name);
    py::list get_output_stream_infos(const std::string &name);
    py::list get_all_stream_infos(const std::string &name);
    py::dict create_configure_params(hailo_stream_interface_t interface);

    const std::unique_ptr<Hef>& hef_ptr() const
    {
        return hef;
    }

    py::dict create_configure_params_mipi_input(hailo_stream_interface_t output_interface,
        const hailo_mipi_input_stream_params_t &mipi_params);
    py::list get_networks_names(const std::string &net_group_name);
    static void initialize_python_module(py::module &m);

private:
    std::unique_ptr<Hef> hef;
};

class ActivatedAppContextManagerWrapper final
{
public:
    ActivatedAppContextManagerWrapper(ConfiguredNetworkGroup &net_group,
        const hailo_activate_network_group_params_t &network_group_params);
    
    const ActivatedNetworkGroup& enter();
    void exit();
    static void add_to_python_module(py::module &m);
private:
    std::unique_ptr<ActivatedNetworkGroup> m_activated_net_group;
    ConfiguredNetworkGroup &m_net_group;
    hailo_activate_network_group_params_t m_network_group_params;
};

} /* namespace hailort */

#endif /* HEF_API_HPP_ */
