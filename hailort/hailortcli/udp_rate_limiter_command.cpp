/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file udp_rate_limiter_command.cpp
 * @brief Rate limiter command impl
 **/

#include "hailo/network_rate_calculator.hpp"
#include "hailo/hailort_common.hpp"
#include "udp_rate_limiter_command.hpp"
#include "hailortcli.hpp"

#include <numeric>
#include <algorithm>

#define PORTS_COUNT (16)     // Should be same as HW_PACKAGE__CORE_PKG__N_AXIS_IN

UdpRateLimiterCommand::UdpRateLimiterCommand (CLI::App &parent_app) :
    Command(parent_app.add_subcommand("udp-rate-limiter", "Limit the UDP rate"))
{
    m_set_command = m_app->add_subcommand("set", "Sets the udp rate limit");
    m_set_command->add_option("--kbit-rate", m_rate_kbit_sec, "rate in Kbit/s")
        ->required();
    m_set_command->add_flag("-f,--force", m_force_reset, "When forced the tool will reset the udp limit before setting it");
    add_device_options(m_set_command, m_board_ip, m_interface_name, m_dport);
    
    m_reset_command = m_app->add_subcommand("reset", "Resets the udp rate limit");
    add_device_options(m_reset_command, m_board_ip, m_interface_name, m_dport);

    m_autoset_command = m_app->add_subcommand("autoset", "Sets the udp limiter based on an existing HEF");
    m_autoset_command->add_option("--hef", m_hef_path, "HEF path")
        ->required()
        ->check(CLI::ExistingFile);
    m_autoset_command->add_option("--fps", m_fps, "Required FPS")
        ->required();
    m_autoset_command->add_option("--network-group-name", m_network_group_name, "The name of the network_group to configure rates for");
    m_autoset_command->add_flag("-f,--force", m_force_reset, "When forced the tool will reset the udp limit before setting it");
    add_device_options(m_autoset_command, m_board_ip, m_interface_name, m_dport);

    m_app->require_subcommand(1);
}

hailo_status UdpRateLimiterCommand::execute()
{
    std::vector<uint16_t> board_ports;
    if (0 == m_dport) {
        board_ports = get_dports();
    } else {
        board_ports.emplace_back(m_dport);
    }

    if (m_board_ip.empty()) {
        const auto result = ScanSubcommand::scan_ethernet("", m_interface_name);
        if (!result) {
            return result.status();
        }
        if (0 == result->size()) {
            // scan_ethernet didn't fail but didn't return any ips
            return HAILO_NOT_FOUND;
        }
        m_board_ip = result.value()[0];
    }

    if (m_set_command->parsed()) {
        const auto rate_bytes_sec = bit_rate_kbit_sec_to_bytes_sec(m_rate_kbit_sec);
        return set_command(board_ports, rate_bytes_sec);
    }
    else if (m_reset_command->parsed()) {
        const auto results = reset_commnad(board_ports);
        if (std::any_of(results.cbegin(), results.cend(),
                [](const auto& key_value){ return key_value.second != HAILO_SUCCESS; })) {
            return HAILO_INTERNAL_FAILURE;
        }
        return HAILO_SUCCESS;
    }
    else if (m_autoset_command->parsed()) {
        return autoset_commnad(board_ports);
    }
    // Shouldn't get here
    return HAILO_INTERNAL_FAILURE;
}

hailo_status UdpRateLimiterCommand::set_command(const std::vector<uint16_t> &board_ports, uint32_t rate_bytes_sec)
{
    std::map<uint16_t, hailo_status> reset_results;
    if (m_force_reset) {
        reset_results = reset_commnad(board_ports);
    }

    hailo_status status = HAILO_SUCCESS;
    for (const auto& board_port : board_ports) {
        if ((reset_results.end() != reset_results.find(board_port)) && (HAILO_SUCCESS != reset_results[board_port])) {
            std::cout << "Failed resetting " << m_board_ip << ":" << board_port << "; won't set rate limit." << std::endl;
            continue;
        }
        auto tc = TrafficControlUtil::create(m_board_ip, board_port, rate_bytes_sec);
        if (!tc) {
            // Best effort
            std::cout << "Failed creating TrafficControlUtil for " << m_board_ip << ":" << board_port
                      << " with status " << tc.status() << "; Continuing"<< std::endl;
            status = tc.status();
            continue;
        }

        std::cout << "Setting rate limit to " << rate_bytes_sec << " Bytes/sec for " << m_board_ip << ":" << board_port << "...";
        const auto set_status = tc->set_rate_limit();
        if (HAILO_SUCCESS != set_status) {
            std::cout << std::endl << "Setting rate limit failed with status " << set_status << std::endl;
            status = set_status;
            // Best effort
            continue;
        }
        std::cout << " done." << std::endl;
    }

    return status;
}

std::map<uint16_t, hailo_status> UdpRateLimiterCommand::reset_commnad(const std::vector<uint16_t> &board_ports)
{
    std::map<uint16_t, hailo_status> reset_results;
    for (const auto& board_port : board_ports) {
        auto tc = TrafficControlUtil::create(m_board_ip, board_port, bit_rate_kbit_sec_to_bytes_sec(m_rate_kbit_sec));
        if (!tc) {
            // Best effort
            std::cout << "Failed creating TrafficControlUtil for " << m_board_ip << ":" << board_port
                      << " with status " << tc.status() << "; Continuing"<< std::endl;
            reset_results.emplace(board_port, tc.status());
            continue;
        }

        const auto reset_status = do_reset(tc.value(), m_board_ip, board_port);
        reset_results.emplace(board_port, reset_status);
    }

    return reset_results;
}

hailo_status UdpRateLimiterCommand::autoset_commnad(const std::vector<uint16_t> &board_ports)
{
    TRY(const auto rates_from_hef, calc_rate_from_hef(m_hef_path, m_network_group_name, m_fps));

    // On auto set, we use min rate for all input ports
    auto min_rate_pair = *std::min_element(rates_from_hef.begin(), rates_from_hef.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });

    return set_command(board_ports, static_cast<uint32_t>(min_rate_pair.second));
}

void UdpRateLimiterCommand::add_device_options(CLI::App *app, std::string &board_ip, std::string &interface_name,
    uint16_t &dport)
{
    assert(nullptr != app);

    auto *board_ip_option = app->add_option("--board-ip", board_ip,
        "board ip.\nIf not suplied the board ip will be found by scanning for connected boards.")
        ->check(CLI::ValidIPV4);
    
    auto *interface_name_option = app->add_option("--interface-name", interface_name,
        "Name of the interface on which to scan, if no board ip is provided")
        ->excludes(board_ip_option);

    (void) app->add_option("--dport", dport, "destination port")
        ->default_val(0);

    app->parse_complete_callback([board_ip_option, interface_name_option]() {
        if (board_ip_option->empty() && interface_name_option->empty()) {
            throw CLI::ParseError("Either --board-ip or --interface-name must be set", CLI::ExitCodes::InvalidError);
        }
    });
}


hailo_status UdpRateLimiterCommand::do_reset(TrafficControlUtil &tc, const std::string &board_ip, uint16_t board_port)
{
    std::cout << "Resetting rate limit for " << board_ip << ":" << board_port << "...";
    const auto status = tc.reset_rate_limit();
    if (HAILO_SUCCESS != status) {
        std::cout << std::endl << "Reset rate limit failed with status " << status << std::endl;
        return status;
    }
    std::cout << " done." << std::endl;

    return HAILO_SUCCESS;
}

uint32_t UdpRateLimiterCommand::bit_rate_kbit_sec_to_bytes_sec(uint32_t rate_kbit_sec)
{
    //   rate      * 1000      / 8    = rate * 125
    // (kbit/sec) (bits/kbit) (bits/byte)
    return rate_kbit_sec * 125;
}

Expected<std::map<std::string, uint32_t>> UdpRateLimiterCommand::calc_rate_from_hef(const std::string &hef_path,
    const std::string &network_group_name, uint32_t fps)
{
    TRY(auto hef, Hef::create(hef_path.c_str()), "Failed reading hef file {}", hef_path.c_str());
    TRY(auto rate_calc, NetworkUdpRateCalculator::create(&(hef), network_group_name));
    TRY(auto calculated_rates, rate_calc.calculate_inputs_bandwith(fps));

    return calculated_rates;
}

std::vector<uint16_t> UdpRateLimiterCommand::get_dports()
{
    std::vector<uint16_t> ports(PORTS_COUNT);
    std::iota(ports.begin(), ports.end(), HailoRTCommon::ETH_INPUT_BASE_PORT);

    return ports;
}