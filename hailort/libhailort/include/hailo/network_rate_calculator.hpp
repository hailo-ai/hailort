/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_rate_calculator.hpp
 * @brief Calculate the input bandwidth limit for ethernet streams.
 * The calculation is based on the following parameters:
 * - The frame sizes of the network's input (retrieved from the hef)
 * - The frame sizes of the network's outputs (retrieved from the hef)
 * - The fps
 **/

#ifndef _NETWORK_RATE_CALCULATOR_HPP_
#define _NETWORK_RATE_CALCULATOR_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hef.hpp"
#include "hailo/network_group.hpp"

#include <vector>

/** hailort namespace */
namespace hailort
{

using StreamInfoVector = std::vector<hailo_stream_info_t>;

class HAILORTAPI NetworkUdpRateCalculator {
private:
    std::map<std::string, uint32_t> m_input_edge_shapes;
    std::map<std::string, uint32_t> m_output_edge_shapes;

    static Expected<StreamInfoVector> get_streams_from_hef(Hef* hef, const std::string &network_group_name);

    // Use the static create() function
    NetworkUdpRateCalculator(std::map<std::string, uint32_t> &&input_edge_shapes,
        std::map<std::string, uint32_t> &&output_edge_shapes);

public:
    virtual ~NetworkUdpRateCalculator() = default;
    
    static Expected<NetworkUdpRateCalculator> create(Hef* hef, const std::string &network_group_name="");
    static Expected<NetworkUdpRateCalculator> create(ConfiguredNetworkGroup &net_group);

    /**
     * Calculate the inputs bandwidths supported by the configured network.
     * Rate limiting of this manner is to be used for ethernet input streams.
     *
     * @param[in]     fps                           The desired fps.
     * @param[in]     max_supported_bandwidth       The maximun supported bandwidth.
     *         Neither the calculated input rate, nor the corresponding output rate will exceed this value.
     *         If no value is given, those rates will not exceed @a HAILO_DEFAULT_MAX_ETHERNET_BANDWIDTH_BYTES_PER_SEC.
     * @return Upon success, returns Expected of a map of stream names and their corresponding rates.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note There are two options to limit the rate of an ethernet input stream to the desired bandwidth:
     *       - Set ::hailo_eth_input_stream_params_t.rate_limit_bytes_per_sec inside ::hailo_stream_parameters_t, under NetworkGroupsParamsMap
     *         before passing it to Device::configure.
     *       - On Unix platforms:
     *         - You may use the command line tool `hailortcli udp-rate-limiter` instead of using this API
     */
    Expected<std::map<std::string, uint32_t>> calculate_inputs_bandwith(uint32_t fps,
        uint32_t max_supported_bandwidth = HAILO_DEFAULT_MAX_ETHERNET_BANDWIDTH_BYTES_PER_SEC);

    /**
     * Calculate the inputs bandwidths supported by the configured network, and returns a map of 
     * stream ports and their corresponding rates.
     *
     * @param[in]     udp_input_streams             UDP input streams.
     * @param[in]     fps                           The desired fps.
     * @param[in]     max_supported_bandwidth       The maximun supported bandwidth.
     *         Neither the calculated input rate, nor the corresponding output rate will exceed this value.
     *         If no value is given, those rates will not exceed @a HAILO_DEFAULT_MAX_ETHERNET_BANDWIDTH_BYTES_PER_SEC.
     * @return Upon success, returns Expected of a map of stream ports and their corresponding rates.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::map<uint16_t, uint32_t>> get_udp_ports_rates_dict(
        std::vector<std::reference_wrapper<InputStream>> &udp_input_streams,
        uint32_t fps, uint32_t max_supported_bandwidth = HAILO_DEFAULT_MAX_ETHERNET_BANDWIDTH_BYTES_PER_SEC);

    // Undocumented, exported here for pyhailort usage
    static hailo_status set_rate_limit(const std::string &ip, uint16_t port, uint32_t rate_bytes_per_sec);
    static hailo_status reset_rate_limit(const std::string &ip, uint16_t port);
    static Expected<std::string> get_interface_name(const std::string &ip);
};

} /* namespace hailort */

#endif /* _NETWORK_RATE_CALCULATOR_HPP_ */
