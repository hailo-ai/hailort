/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_defaults.hpp
 * @brief 
 **/

#ifndef _HAILO_HAILORT_DEFAULTS_HPP_
#define _HAILO_HAILORT_DEFAULTS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/network_group.hpp"

/** hailort namespace */
namespace hailort
{

#define HAILO_DEFAULT_NETWORK_NAME_QUALIFIER (std::string("/"))


class HAILORTAPI HailoRTDefaults final
{
public:
    HailoRTDefaults() = delete;

    static Expected<hailo_format_order_t> get_device_format_order(uint32_t compiler_format_order);
    static hailo_format_order_t get_default_host_format_order(const hailo_format_t &device_format);

    static hailo_format_t expand_auto_format(const hailo_format_t &host_format, const hailo_format_t &hw_format);
    static hailo_format_t get_user_buffer_format();
    static hailo_format_t get_user_buffer_format(bool unused, hailo_format_type_t format_type);

    static hailo_transform_params_t get_transform_params(bool unused, hailo_format_type_t format_type);
    static hailo_transform_params_t get_transform_params(const hailo_stream_info_t &stream_info);
    static hailo_transform_params_t get_transform_params();

    static hailo_vstream_params_t get_vstreams_params();
    static hailo_vstream_params_t get_vstreams_params(bool unused, hailo_format_type_t format_type);

    static Expected<hailo_stream_parameters_t> get_stream_parameters(hailo_stream_interface_t interface,
            hailo_stream_direction_t direction);

    static ConfigureNetworkParams get_configure_params(uint16_t batch_size = HAILO_DEFAULT_BATCH_SIZE,
        hailo_power_mode_t power_mode = HAILO_POWER_MODE_PERFORMANCE);
    static hailo_network_parameters_t get_network_parameters(uint16_t batch_size = HAILO_DEFAULT_BATCH_SIZE);
    static std::string get_network_name(const std::string &net_group_name);
    static hailo_activate_network_group_params_t get_active_network_group_params();

    static hailo_vdevice_params_t get_vdevice_params();

private:
    static struct sockaddr_in get_sockaddr();
    static hailo_eth_input_stream_params_t get_eth_input_stream_params();
    static hailo_eth_output_stream_params_t get_eth_output_stream_params();
    static hailo_pcie_input_stream_params_t get_pcie_input_stream_params();
    static hailo_pcie_output_stream_params_t get_pcie_output_stream_params();
    static hailo_integrated_input_stream_params_t get_integrated_input_stream_params();
    static hailo_integrated_output_stream_params_t get_integrated_output_stream_params();
    static hailo_mipi_input_stream_params_t get_mipi_input_stream_params();
};

} /* namespace hailort */

#endif /* _HAILO_HAILORT_DEFAULTS_HPP_ */
