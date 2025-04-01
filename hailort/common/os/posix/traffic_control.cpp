/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file traffic_control.cpp
 * @brief Traffic Control wrapper
 **/

#include "traffic_control.hpp"
#include "common/process.hpp"
#include "common/ethernet_utils.hpp"
#include "common/utils.hpp"
#include "hailo/buffer.hpp"
#include "byte_order.h"

#include <sstream>
#include <thread>

namespace hailort
{

Expected<TrafficControlUtil> TrafficControlUtil::create(const std::string &ip, uint16_t port, uint32_t rate_bytes_per_sec)
{
    TRY(const auto interface_name, EthernetUtils::get_interface_from_board_ip(ip), "get_interface_name failed");
    TRY(const auto board_id, ip_to_board_id(ip), "ip_to_board_id failed");
    TRY(const auto is_sudo_needed, check_is_sudo_needed(), "check_is_sudo_needed failed");

    return TrafficControlUtil(ip, interface_name, board_id, port, port_to_port_id(port),
        rate_bytes_per_sec, is_sudo_needed);
}

TrafficControlUtil::TrafficControlUtil(const std::string& board_address, const std::string& interface_name,
        uint32_t board_id, uint16_t board_port, uint16_t port_id, uint32_t rate_bytes_per_sec, bool is_sudo_needed) :
    m_board_address(board_address),
    m_interface_name(interface_name),
    m_board_id(board_id),
    m_board_port(board_port),
    m_port_id(port_id),
    m_rate_bytes_per_sec(rate_bytes_per_sec),
    m_is_sudo_needed(is_sudo_needed)
{}

hailo_status TrafficControlUtil::set_rate_limit()
{
    LOGGER__INFO("Setting UDP rate to {} Byte/sec for {}:{}", m_rate_bytes_per_sec, m_board_address, m_board_port);
    auto status = add_board_to_interface();
    CHECK_SUCCESS(status, "add_board_to_interface failed with status {}", status);
    status = add_input_to_inteface();
    CHECK_SUCCESS(status, "add_input_to_inteface failed with status {}", status);
    return status;
}

hailo_status TrafficControlUtil::reset_rate_limit()
{
    LOGGER__INFO("Resetting UDP rate for {}:{}", m_board_address, m_board_port);
    // Best effort
    const auto del_input_status = del_input();
    const auto del_board_status = del_board();

    CHECK_SUCCESS(del_input_status);
    CHECK_SUCCESS(del_board_status);
    return HAILO_SUCCESS;
}

hailo_status TrafficControlUtil::add_board_to_interface()
{
    CHECK_SUCCESS(tc_qdisc_add_dev(m_interface_name));
    CHECK_SUCCESS(tc_class_add_dev_for_board(m_interface_name, m_board_id));
    return HAILO_SUCCESS;
}

hailo_status TrafficControlUtil::add_input_to_inteface()
{
    CHECK_SUCCESS(tc_class_add_dev_for_input(m_interface_name, m_board_id, m_port_id, m_rate_bytes_per_sec));
    CHECK_SUCCESS(tc_filter_add_dev_for_input(m_interface_name, m_board_address, m_port_id, m_board_port));
    return HAILO_SUCCESS;
}

hailo_status TrafficControlUtil::del_input()
{
    static const auto SLEEP_BETWEEN_DELS = std::chrono::milliseconds(200);
    CHECK_SUCCESS(tc_filter_del_dev_for_input(m_interface_name, m_board_address, m_port_id, m_board_port));
    std::this_thread::sleep_for(SLEEP_BETWEEN_DELS);
    CHECK_SUCCESS(tc_class_del_dev_for_input(m_interface_name, m_board_id, m_port_id));
    return HAILO_SUCCESS;
}

hailo_status TrafficControlUtil::del_board()
{
    return tc_class_del_dev_for_board(m_interface_name, m_board_id);
}

hailo_status TrafficControlUtil::tc_qdisc_add_dev(const std::string &interface_name)
{
    // Right now we do not delete the qdisc itself on cleanup
    // Hence, it's OK if the QDISC configuration already exists
    // On 18.04 the error is a bit different so adding another allowed error
    std::stringstream cmd;
    cmd << "tc qdisc add dev " << interface_name << " root handle 1: htb default 10 direct_qlen 2000";
    return run_command(cmd.str(), m_is_sudo_needed,
        {"RTNETLINK answers: File exists", "Error: Exclusivity flag on, cannot modify."});
}

hailo_status TrafficControlUtil::tc_class_add_dev_for_board(const std::string &interface_name, uint32_t board_id)
{
    std::stringstream cmd;
    cmd << "tc class add dev " << interface_name << " parent 1: classid 1:" << board_id << " htb rate 1Gbit";
    return run_command(cmd.str(), m_is_sudo_needed,
        {"RTNETLINK answers: File exists", "Error: Exclusivity flag on, cannot modify."});
}

hailo_status TrafficControlUtil::tc_class_add_dev_for_input(const std::string &interface_name, uint32_t board_id,
    uint16_t port_id, uint32_t rate_bytes_per_sec)
{
    std::stringstream cmd;
    cmd << "tc class add dev " << interface_name << " parent 1:" << board_id << " classid 1:" << port_id
        << " htb rate " << rate_bytes_per_sec << "bps ceil " << rate_bytes_per_sec << "bps maxburst 1kb";
    return run_command(cmd.str(), m_is_sudo_needed);
}

hailo_status TrafficControlUtil::tc_filter_add_dev_for_input(const std::string &interface_name,
    const std::string &board_ip, uint16_t port_id, uint16_t board_port)
{
    std::stringstream cmd;
    cmd << "tc filter add dev " << interface_name << " protocol ip parent 1:0 prio 1 u32 match ip dst " << board_ip
        << " match ip dport " << board_port << " 0xffff flowid 1:" << port_id;
    return run_command(cmd.str(), m_is_sudo_needed);
}

hailo_status TrafficControlUtil::tc_filter_del_dev_for_input(const std::string &interface_name,
    const std::string &board_ip, uint16_t port_id, uint16_t board_port)
{
    std::stringstream cmd;
    cmd << "tc filter del dev " << interface_name << " protocol ip parent 1:0 prio 1 u32 match ip dst "
        << board_ip << " match ip dport " << board_port << " 0xffff flowid 1:" << port_id;
    return run_command(cmd.str(), m_is_sudo_needed, {}, true);
}

hailo_status TrafficControlUtil::tc_class_del_dev_for_input(const std::string &interface_name,
    uint32_t board_id, uint16_t port_id)
{
    std::stringstream cmd;
    cmd << "tc class del dev " << interface_name << " parent 1:" << board_id << " classid 1:" << port_id;
    return run_command(cmd.str(), m_is_sudo_needed, {}, true);
}

hailo_status TrafficControlUtil::tc_class_del_dev_for_board(const std::string &interface_name,
    uint32_t board_id)
{
    std::stringstream cmd;
    cmd << "tc class del dev " << interface_name << " parent 1: classid 1:" << board_id;
    return run_command(cmd.str(), m_is_sudo_needed, {}, true);
}

Expected<uint32_t> TrafficControlUtil::ip_to_board_id(const std::string &ip)
{
    // Takes last digit from 3 octet + the whole 4th octet
    // We try to get a unique id
    const auto last_dot_loc = ip.find_last_of('.');
    if (std::string::npos == last_dot_loc) {
        LOGGER__ERROR("\".\" character not found in ip=\"{}\"", ip.c_str());
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
    std::string board_id_str = ip[last_dot_loc - 1] + ip.substr(last_dot_loc + 1);
    uint32_t board_id = std::atoi(board_id_str.c_str());
    if (0 == board_id) {
        LOGGER__ERROR("atoi failed parsing \"{}\"", board_id_str.c_str());
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    return board_id;
}

uint16_t TrafficControlUtil::port_to_port_id(uint16_t port)
{
    // We use % to make the id unique (hopefully)
    return port % 1000;
}

Expected<bool> TrafficControlUtil::check_is_sudo_needed()
{
    TRY(const auto result, Process::create_and_wait_for_output("id -u", MAX_COMMAND_OUTPUT_LENGTH));
    
    // If the user id is zero then we don't need to add `sudo` to our commands
    return std::move(result.second != "0");
}

hailo_status TrafficControlUtil::run_command(const std::string &commnad, bool add_sudo,
    const std::vector<std::string> &allowed_errors, bool ignore_fails)
{
    // Note: we redirect stderr to stdout
    TRY(const auto result, Process::create_and_wait_for_output(
        add_sudo ? "sudo " + commnad + " 2>&1" : commnad + " 2>&1",
        MAX_COMMAND_OUTPUT_LENGTH));

    const uint32_t exit_code = result.first;
    if (0 == exit_code) {
        return HAILO_SUCCESS;
    }

    std::string cmd_output = result.second;
    // No output = everything was OK
    bool is_output_valid = cmd_output.empty();
    if ((!is_output_valid) && (!allowed_errors.empty())) {
        is_output_valid = (std::find(allowed_errors.cbegin(), allowed_errors.cend(), cmd_output) != allowed_errors.cend());
    }

    if (is_output_valid || ignore_fails) {
        LOGGER__TRACE("Commnad \"{}\" returned a non-zero exit code ({});"
                      "ignoring (ignore_fails={}, is_output_valid={}).",
                      cmd_output.c_str(), exit_code, is_output_valid, ignore_fails);
        return HAILO_SUCCESS;
    }
    LOGGER__ERROR("Commnad \"{}\" returned a non-zero exit code ({}), failing.",  cmd_output.c_str(), exit_code);
    return HAILO_TRAFFIC_CONTROL_FAILURE;
}

Expected<TrafficControl> TrafficControl::create(const std::string &ip, uint16_t port, uint32_t rate_bytes_per_sec)
{
    TRY(auto tc_util, TrafficControlUtil::create(ip, port, rate_bytes_per_sec));
    
    hailo_status rate_set_status = HAILO_UNINITIALIZED;
    TrafficControl tc(std::move(tc_util), rate_set_status);
    CHECK_SUCCESS_AS_EXPECTED(rate_set_status, "Failed setting rate limit with status {}", rate_set_status);

    return tc;
}

TrafficControl::~TrafficControl()
{
    if (m_call_reset) {
        const auto status = m_tc_util.reset_rate_limit();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("reset_rate_limit failed with status={}", status);
        }
    }
}

TrafficControl::TrafficControl(TrafficControl &&other) :
    m_tc_util(std::move(other.m_tc_util)),
    m_call_reset(std::exchange(other.m_call_reset, false))
{}

TrafficControl::TrafficControl(TrafficControlUtil &&tc, hailo_status &rate_set_status) :
    m_tc_util(std::move(tc)),
    m_call_reset(false)
{
    rate_set_status = m_tc_util.reset_rate_limit();
    if (HAILO_SUCCESS != rate_set_status) {
        // This should succeed if there were previous TC rules set or if there weren't any
        // Hence, we won't continue
        LOGGER__ERROR("reset_rate_limit failed with status={}", rate_set_status);
        return;
    }
    // We want to call reset in the dtor even if set_rate_limit fails
    m_call_reset = true;
    rate_set_status = m_tc_util.set_rate_limit();
    if (HAILO_SUCCESS != rate_set_status) {
        LOGGER__ERROR("set_rate_limit failed with status={}", rate_set_status);
        return;
    }
}

} /* namespace hailort */
