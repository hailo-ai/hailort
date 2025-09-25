/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file udp_rate_limiter_command.hpp
 * @brief Tool for limiting the packet sending rate via UDP. Needed to ensure the board will not get more
 * traffic than it can handle, which would cause packet loss.
 **/

#ifndef _HAILO_UDP_RATE_LIMITER_COMMAND_HPP_
#define _HAILO_UDP_RATE_LIMITER_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "scan_command.hpp"
#if defined(__GNUC__)
#include "common/os/posix/traffic_control.hpp"
#endif

#include "CLI/CLI.hpp"

#include <map>


class UdpRateLimiterCommand : public Command {
public:
    explicit UdpRateLimiterCommand(CLI::App &parent_app);
    hailo_status execute() override;

private:
    hailo_status set_command(const std::vector<uint16_t> &board_ports, uint32_t rate_bytes_sec);
    std::map<uint16_t, hailo_status> reset_commnad(const std::vector<uint16_t> &board_ports);
    hailo_status autoset_commnad(const std::vector<uint16_t> &board_ports);

    static void add_device_options(CLI::App *app, std::string &board_ip, std::string &interface_name, uint16_t &dport);
    static hailo_status do_reset(TrafficControlUtil &tc, const std::string &board_ip, uint16_t board_port);
    static uint32_t bit_rate_kbit_sec_to_bytes_sec(uint32_t rate_kbit_sec);
    static Expected<std::map<std::string, uint32_t>> calc_rate_from_hef(const std::string &hef_path, const std::string &network_group_name,
        uint32_t fps);
    static std::vector<uint16_t> get_dports();

    CLI::App *m_set_command;
    CLI::App *m_reset_command;
    CLI::App *m_autoset_command;
    std::string m_board_ip;
    std::string m_interface_name;
    uint16_t m_dport;
    uint32_t m_rate_kbit_sec;
    bool m_force_reset;
    std::string m_hef_path;
    std::string m_network_group_name;
    uint32_t m_fps;
};

#endif /* _HAILO_UDP_RATE_LIMITER_COMMAND_HPP_ */
