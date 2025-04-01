/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file traffic_control.hpp
 * @brief Traffic Control wrapper
 *
 * 
 **/
#ifndef _TRAFFIC_CONTROL_HPP_
#define _TRAFFIC_CONTROL_HPP_

#include "common/utils.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"

namespace hailort
{

// Contains helper functions to work with the Traffic control command line tool
class TrafficControlUtil final
{
public:
    static Expected<TrafficControlUtil> create(const std::string &ip, uint16_t port, uint32_t rate_bytes_per_sec);
    ~TrafficControlUtil() = default;
    TrafficControlUtil(TrafficControlUtil&) = delete;
    TrafficControlUtil &operator=(const TrafficControlUtil &) = delete;
    TrafficControlUtil &operator=(TrafficControlUtil &&) = delete;
    TrafficControlUtil(TrafficControlUtil &&other) = default;
    
    hailo_status set_rate_limit();
    hailo_status reset_rate_limit();

private:
    TrafficControlUtil(const std::string& board_address, const std::string& interface_name,
        uint32_t board_id, uint16_t board_port, uint16_t port_id, uint32_t rate_bytes_per_sec, bool is_sudo_needed);

    hailo_status add_board_to_interface();
    hailo_status add_input_to_inteface();
    hailo_status del_input();
    hailo_status del_board();

    hailo_status tc_qdisc_add_dev(const std::string &interface_name);
    hailo_status tc_class_add_dev_for_board(const std::string &interface_name, uint32_t board_id);
    hailo_status tc_class_add_dev_for_input(const std::string &interface_name, uint32_t board_id,
        uint16_t port_id, uint32_t rate_bytes_per_sec);
    hailo_status tc_filter_add_dev_for_input(const std::string &interface_name, const std::string &board_ip,
        uint16_t port_id, uint16_t board_port);
    hailo_status tc_filter_del_dev_for_input(const std::string &interface_name, const std::string &board_ip,
        uint16_t port_id, uint16_t board_port);
    hailo_status tc_class_del_dev_for_input(const std::string &interface_name, uint32_t board_id,
        uint16_t port_id);
    hailo_status tc_class_del_dev_for_board(const std::string &interface_name, uint32_t board_id);

    static const uint32_t MAX_COMMAND_OUTPUT_LENGTH = 100;
    static Expected<std::string> get_interface_address(const struct in_addr *addr);

    static Expected<uint32_t> ip_to_board_id(const std::string &ip);
    static uint16_t port_to_port_id(uint16_t port);
    static Expected<bool> check_is_sudo_needed();
    static hailo_status run_command(const std::string &commnad, bool add_sudo,
        const std::vector<std::string> &allowed_errors = {}, bool ignore_fails = false);

    const std::string m_board_address;
    const std::string m_interface_name;
    const uint32_t m_board_id;
    const uint16_t m_board_port;
    const uint16_t m_port_id;
    const uint32_t m_rate_bytes_per_sec;
    const bool m_is_sudo_needed;
};

// RAII for TrafficControlUtil
class TrafficControl final 
{
public:
    static Expected<TrafficControl> create(const std::string &ip, uint16_t port, uint32_t rate_bytes_per_sec);
    ~TrafficControl();
    TrafficControl(TrafficControl&) = delete;
    TrafficControl &operator=(const TrafficControl &) = delete;
    TrafficControl &operator=(TrafficControl &&) = delete;
    TrafficControl(TrafficControl &&other);

private:
    TrafficControl(TrafficControlUtil &&tc, hailo_status &rate_set_status);

    TrafficControlUtil m_tc_util;
    bool m_call_reset;
};

} /* namespace hailort */

#endif  /* _TRAFFIC_CONTROL_HPP_ */
