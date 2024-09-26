/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_control.cpp
 * @brief Several controls that can be sent to the firware
 **/

#include "fw_control_command.hpp"
#include "firmware_header_utils.h"
#include "common/string_utils.hpp"


static const char *NOT_CONFIGURED_ATTR = "<N/A>";
#define MHz (1000 * 1000)


static std::string extended_device_information_boot_string(hailo_device_boot_source_t boot_source)
{
    switch (boot_source) {
    case HAILO_DEVICE_BOOT_SOURCE_PCIE:
        return "PCIE";
    case HAILO_DEVICE_BOOT_SOURCE_FLASH:
        return "FLASH";
    default:
        return "Unknown";
    }
}

static std::string extended_device_information_supported_features(hailo_device_supported_features_t supported_features)
{
    std::string supported_features_str;

    if(supported_features.current_monitoring) {
        supported_features_str.append("Current Monitoring, ");
    }
    if(supported_features.ethernet) {
        supported_features_str.append("Ethernet, ");
    }
    if(supported_features.mipi) {
        supported_features_str.append("MIPI, ");
    }
    if(supported_features.mdio) {
        supported_features_str.append("MDIO, ");
    }
    if(supported_features.pcie) {
        supported_features_str.append("PCIE, ");
    }

    std::size_t last_comma_location = supported_features_str.find_last_of(",");
    supported_features_str = supported_features_str.substr(0,last_comma_location);

    return supported_features_str;
}

static void extended_device_information_print_array(uint8_t *array_for_print, size_t array_length, std::string splitter)
{
    const bool UPPERCASE = true;
    std::cout << StringUtils::to_hex_string(array_for_print, array_length, UPPERCASE, splitter) << std::endl;
}

static bool extended_device_information_is_array_not_empty(uint8_t *array_for_print, size_t array_length)
{
    uint32_t i = 0;
    for(i = 0; i < array_length; i++) {
        if(array_for_print[i] != 0){
            return true;
        }
    }
    return false;
}

static hailo_status print_extended_device_information(Device &device)
{
    TRY(auto device_info, device.get_extended_device_information());

    // Print Board Extended information
    std::cout << "Boot source: " << extended_device_information_boot_string(device_info.boot_source) << std::endl;
    std::cout << "Neural Network Core Clock Rate: " << (device_info.neural_network_core_clock_rate/MHz) <<"MHz" <<std::endl;

    std::string supported_features_str = extended_device_information_supported_features(device_info.supported_features);
    if(supported_features_str.length() > 0) {
        std::cout << "Device supported features: " << supported_features_str << std::endl;
    }
    std::cout << "LCS: " << static_cast<int>(device_info.lcs) << std::endl;

    if(extended_device_information_is_array_not_empty(device_info.soc_id, sizeof(device_info.soc_id))){
        std::cout << "SoC ID: ";
        extended_device_information_print_array(device_info.soc_id, sizeof(device_info.soc_id), "");
    }

    if(extended_device_information_is_array_not_empty(device_info.eth_mac_address, sizeof(device_info.eth_mac_address))){
        std::cout << "MAC Address: ";
        extended_device_information_print_array(device_info.eth_mac_address, sizeof(device_info.eth_mac_address), ":");
    }

    if(extended_device_information_is_array_not_empty(device_info.unit_level_tracking_id, sizeof(device_info.unit_level_tracking_id))){
        std::cout << "ULT ID: ";
        extended_device_information_print_array(device_info.unit_level_tracking_id, sizeof(device_info.unit_level_tracking_id), "");
    }

    if(extended_device_information_is_array_not_empty(device_info.soc_pm_values, sizeof(device_info.soc_pm_values))){
        std::cout << "PM Values: ";
        extended_device_information_print_array(device_info.soc_pm_values, sizeof(device_info.soc_pm_values), "");
    }

    return HAILO_SUCCESS;
}

static std::string fw_version_string(const hailo_device_identity_t &identity)
{
    std::stringstream os;
    const auto fw_mode = ((identity.is_release) ? "release" : "develop");
    // TODO: Currently will always return FW_BINARY_TYPE_APP_FIRMWARE as version bit is cleared in HailoRT
    FW_BINARY_TYPE_t fw_binary_type = FIRMWARE_HEADER_UTILS__get_fw_binary_type(identity.fw_version.revision);
    auto fw_type = "invalid";
    if (FW_BINARY_TYPE_CORE_FIRMWARE == fw_binary_type) {
        fw_type = "core";
    } else if (FW_BINARY_TYPE_APP_FIRMWARE == fw_binary_type) {
        fw_type = "app";
    }
    os << identity.fw_version.major << "." << identity.fw_version.minor << "."
       << identity.fw_version.revision << " (" << fw_mode << "," << fw_type;
    if (identity.extended_context_switch_buffer) {
        os << ",extended context switch buffer";
    }
    os << ")";
    return os.str();
}

static std::string identity_arch_string(const hailo_device_identity_t &identity)
{
    switch (identity.device_architecture) {
    case HAILO_ARCH_HAILO8:
        return "HAILO8";
    case HAILO_ARCH_HAILO8L:
        return "HAILO8L";
    case HAILO_ARCH_HAILO15H:
        return "HAILO15H";
    case HAILO_ARCH_PLUTO:
        return "PLUTO";
    case HAILO_ARCH_HAILO15M:
        return "HAILO15M";
    case HAILO_ARCH_HAILO10H:
        return "HAILO10H";
    default:
        return "Unknown";
    }
}

static std::string identity_attr_string(const char *attr, size_t attr_max_len)
{
    size_t actual_len = strnlen(attr, attr_max_len);
    if (actual_len == 0) {
        return  NOT_CONFIGURED_ATTR;
    }
    return std::string(attr, actual_len);
}

FwControlIdentifyCommand::FwControlIdentifyCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("identify", "Displays general information about the device")),
    m_is_extended(false)
{
    m_app->add_flag("--extended", m_is_extended, "Print device extended information");
}

hailo_status FwControlIdentifyCommand::execute_on_device(Device &device)
{
    TRY(const auto identity, device.identify());

    // Print board information
    std::cout << "Identifying board" << std::endl;
    std::cout << "Control Protocol Version: " << identity.protocol_version << std::endl;
    std::cout << "Firmware Version: " << fw_version_string(identity) << std::endl;
    std::cout << "Logger Version: " << identity.logger_version << std::endl;
    std::cout << "Board Name: " << std::string(identity.board_name, identity.board_name_length) << std::endl;
    std::cout << "Device Architecture: " << identity_arch_string(identity) << std::endl;
    std::cout << "Serial Number: " <<
        identity_attr_string(identity.serial_number, identity.serial_number_length) << std::endl;
    std::cout << "Part Number: " <<
        identity_attr_string(identity.part_number, identity.part_number_length) << std::endl;
    std::cout << "Product Name: " <<
        identity_attr_string(identity.product_name, identity.product_name_length) << std::endl;

    if (m_is_extended) {
        print_extended_device_information(device);
    }

    std::cout << std::endl;
    return HAILO_SUCCESS;
}

FwControlResetCommand::FwControlResetCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("reset", "Resets the device"))
{
    m_app->add_option("--reset-type", m_reset_mode, "Reset type")
        ->required()
        ->transform(HailoCheckedTransformer<hailo_reset_device_mode_t>({
            { "chip", HAILO_RESET_DEVICE_MODE_CHIP  },
            { "nn_core", HAILO_RESET_DEVICE_MODE_NN_CORE },
            { "soft", HAILO_RESET_DEVICE_MODE_SOFT },
            { "forced_soft", HAILO_RESET_DEVICE_MODE_FORCED_SOFT },
        }));
}

hailo_status FwControlResetCommand::execute_on_device(Device &device)
{
    auto status = device.reset(m_reset_mode);
    CHECK_SUCCESS(status, "Failed reset device");

    std::cout << "Board has been reset successfully" << std::endl;
    return HAILO_SUCCESS;
}

FwControlTestMemoriesCommand::FwControlTestMemoriesCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("test-memories", "Run a test of the chip's memories"))
{}

hailo_status FwControlTestMemoriesCommand::execute_on_device(Device &device)
{
    auto status = device.test_chip_memories();
    CHECK_SUCCESS(status, "Failed memory test");

    std::cout << "Memory test has completed successfully" << std::endl;
    return HAILO_SUCCESS;
}

FwControlDebugHaltContinueCommand::FwControlDebugHaltContinueCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("continue", "Continue breakpoint action"))
{}

hailo_status FwControlDebugHaltContinueCommand::execute_on_device(Device &device)
{
    auto status = device.continue_context_switch_breakpoint(0);
    CHECK_SUCCESS(status, "Failed to excute debug operation");

    std::cout << "Control Operation Debug Continue completed successfully" << std::endl;
    return HAILO_SUCCESS;
}

FwControlDebugCommand::FwControlDebugCommand(CLI::App &parent_app) :
    ContainerCommand(parent_app.add_subcommand("debug", "Access to usefull debug operations"))
{
    add_subcommand<FwControlDebugHaltContinueCommand>();
}

FwControlCommand::FwControlCommand(CLI::App &parent_app) :
    ContainerCommand(parent_app.add_subcommand("fw-control", "Useful firmware control operations"))
{
    add_subcommand<FwControlIdentifyCommand>();
    add_subcommand<FwControlResetCommand>(OptionVisibility::HIDDEN);
    add_subcommand<FwControlTestMemoriesCommand>(OptionVisibility::HIDDEN);
    add_subcommand<DownloadActionListCommand>(OptionVisibility::HIDDEN);
    add_subcommand<FwControlDebugCommand>(OptionVisibility::HIDDEN);
}
