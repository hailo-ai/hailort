/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailortcli.cpp
 * @brief HailoRT CLI.
 *
 * HailoRT command line interface.
 **/
#include "hailortcli.hpp"
#include "scan_command.hpp"
#include "power_measurement_command.hpp"
#include "run_command.hpp"
#include "fw_update_command.hpp"
#include "ssb_update_command.hpp"
#include "sensor_config_command.hpp"
#include "board_config_command.hpp"
#include "fw_config_command.hpp"
#include "fw_logger_command.hpp"
#include "benchmark_command.hpp"
#if defined(__GNUC__)
#include "udp_rate_limiter_command.hpp"
#endif
#include "parse_hef_command.hpp"
#include "fw_control_command.hpp"

#include "firmware_header_utils.h"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/device.hpp"
#include "hailo/hef.hpp"
#include "hailo/buffer.hpp"

#include "CLI/CLI.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <iostream>
#include <vector>
#include <thread>
#include <map>

static Expected<hailo_pcie_device_info_t> get_pcie_device_info(const hailo_pcie_params &pcie_params)
{
    if (pcie_params.pcie_bdf.empty()) {
        auto scan_result = Device::scan_pcie();
        if (!scan_result) {
            std::cerr << "Hailo PCIe scan failed (maybe pcie device not exists). status=" << scan_result.status() << std::endl;
            return make_unexpected(scan_result.status());
        }

        if (scan_result->size() == 0) {
            std::cerr << "Hailo PCIe not found.." << std::endl;
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
        return std::move(scan_result->at(0));
    } else {
        auto device_info_expected = Device::parse_pcie_device_info(pcie_params.pcie_bdf);
        if (!device_info_expected) {
            std::cerr << "Invalid pcie bdf format" << std::endl;
            return make_unexpected(device_info_expected.status());
        }
        return device_info_expected.release();
    }
}

Expected<std::unique_ptr<Device>> create_pcie_device(const hailo_pcie_params &pcie_params)
{
    auto device_info = get_pcie_device_info(pcie_params);
    if (!device_info) {
        return make_unexpected(device_info.status());
    }

    auto device = Device::create_pcie(device_info.value());
    if (!device) {
        std::cerr << "Failed create pcie device. status=" << device.status() << std::endl;
        return make_unexpected(device.status());
    }

    return Expected<std::unique_ptr<Device>>(device.release());
}

static Expected<std::unique_ptr<Device>> create_eth_device(const hailo_eth_params &eth_params)
{
    auto device = Device::create_eth(eth_params.ip_addr);
    if (!device) {
        std::cerr << "Failed create ethernet device. status=" << device.status() << std::endl;
        return make_unexpected(device.status());
    }

    return Expected<std::unique_ptr<Device>>(device.release());
}

Expected<std::unique_ptr<Device>> create_device(const hailo_device_params &device_params)
{
    switch (device_params.device_type) {
    case DeviceType::PCIE:
        return create_pcie_device(device_params.pcie_params);
    case DeviceType::ETH:
        return create_eth_device(device_params.eth_params);
    case DeviceType::DEFAULT:
        // If core driver is loaded (we are running on Mercury) then the default is core device; else, pcie device
        if (Device::is_core_driver_loaded()) {
            return Device::create_core_device();
        } else {
            return create_pcie_device(device_params.pcie_params);
        }
    default:
        std::cerr << "Invalid device type" << std::endl;
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

void add_device_options(CLI::App *app, hailo_device_params &device_params)
{
    // Initialize the device type to default
    device_params.device_type = DeviceType::DEFAULT;

    auto group = app->add_option_group("Device Options");
    
    const HailoCheckedTransformer<DeviceType> device_type_transformer({
            { "pcie", DeviceType::PCIE },
            { "eth", DeviceType::ETH },
        });
    auto *device_type_option = group->add_option("-d,--device-type,--target", device_params.device_type,
        "Device type to use\n"
        "Default is pcie.")
        ->transform(device_type_transformer);

    // PCIe options
    auto *pcie_bdf_option = group->add_option("-s,--bdf", device_params.pcie_params.pcie_bdf,
        "Device id ([<domain>]:<bus>:<device>.<func>, same as in lspci command).\n" \
        "In order to run on all PCIe devices connected to the machine one-by-one, use '*' (instead of device id).")
        ->default_val("");

    // Ethernet options
    auto *ip_option = group->add_option("--ip", device_params.eth_params.ip_addr, "IP address of the target")
        ->default_val("")
        ->check(CLI::ValidIPV4);

    group->parse_complete_callback([&device_params, device_type_option, pcie_bdf_option, ip_option](){
        // The user didn't put target, we can figure it ourself
        if (device_type_option->empty()) {
            if (!ip_option->empty()) {
                // User gave IP, target is eth
                device_params.device_type = DeviceType::ETH;
            } else if (!pcie_bdf_option->empty()) {
                // User gave bdf, target is pcie
                device_params.device_type = DeviceType::PCIE;
            }
            else {
                device_params.device_type = DeviceType::DEFAULT;
            }
        }

        if (ip_option->empty() && device_params.device_type == DeviceType::ETH) {
            throw CLI::ParseError("IP address is not set", CLI::ExitCodes::InvalidError);
        }

        if (!ip_option->empty() && device_params.device_type != DeviceType::ETH) {
            throw CLI::ParseError("IP address is set on non eth device", CLI::ExitCodes::InvalidError);
        }

        if (!pcie_bdf_option->empty() && device_params.device_type != DeviceType::PCIE) {
            throw CLI::ParseError("bdf (-s) is set on non pcie device", CLI::ExitCodes::InvalidError);
        }
    });
}

void add_vdevice_options(CLI::App *app, hailo_device_params &device_params) {
    auto group = app->add_option_group("VDevice Options");

    // VDevice options
    auto *device_count_option = group->add_option("--device-count", device_params.vdevice_params.device_count, "VDevice device count")
        ->default_val(HAILO_DEFAULT_DEVICE_COUNT)
        ->check(CLI::PositiveNumber);

    group->parse_complete_callback([&device_params, device_count_option](){
        // The user gave device_count
        if (!device_count_option->empty()) {
            if ((device_params.vdevice_params.device_count > 1) &&
                ((DeviceType::ETH == device_params.device_type) || (DeviceType::PCIE == device_params.device_type && !device_params.pcie_params.pcie_bdf.empty()))) {
                    throw CLI::ParseError("Device type must not be specified when using multiple devices", CLI::ExitCodes::InvalidError);
            }
        }
    });
}

static bool do_versions_match()
{
    hailo_version_t libhailort_version = {};
    auto status = hailo_get_library_version(&libhailort_version);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to get libhailort version" << std::endl;
        return false;
    }

    bool versions_match = ((HAILORT_MAJOR_VERSION == libhailort_version.major) &&
        (HAILORT_MINOR_VERSION == libhailort_version.minor) &&
        (HAILORT_REVISION_VERSION == libhailort_version.revision));
    if (!versions_match) {
        std::cerr << "libhailort version (" <<
            libhailort_version.major << "." << libhailort_version.minor << "." << libhailort_version.revision <<
            ") does not match HailoRT-CLI version (" <<
            HAILORT_MAJOR_VERSION << "." << HAILORT_MINOR_VERSION << "." << HAILORT_REVISION_VERSION << ")" << std::endl;
        return false;
    }
    return true;
}

class HailoRTCLI : public ContainerCommand {
public:
    HailoRTCLI(CLI::App *app) : ContainerCommand(app)
    {

        m_app->add_flag_callback("-v,--version",
            [] () {
                std::cout << "HailoRT-CLI version " <<
                    HAILORT_MAJOR_VERSION << "." << HAILORT_MINOR_VERSION << "." << HAILORT_REVISION_VERSION << std::endl;
                // throw CLI::Success to stop parsing and not failing require_subcommand(1) we set earlier
                throw (CLI::Success{});
            },
            "Print program version and exit"
        );

        add_subcommand<RunCommand>();
        add_subcommand<ScanSubcommand>();
        add_subcommand<BenchmarkCommand>();
        add_subcommand<PowerMeasurementSubcommand>();
        add_subcommand<SensorConfigCommand>();
        add_subcommand<BoardConfigCommand>();
        add_subcommand<FwConfigCommand>();
        add_subcommand<FwLoggerCommand>();
        add_subcommand<FwUpdateCommand>();
        add_subcommand<SSBUpdateCommand>();
#if defined(__GNUC__)
        add_subcommand<UdpRateLimiterCommand>();
#endif
        add_subcommand<ParseHefCommand>();
        add_subcommand<FwControlCommand>();
    }

    int parse_and_execute(int argc, char **argv)
    {
        CLI11_PARSE(*m_app, argc, argv);
        return execute();
    }

};

int main(int argc, char** argv) {
    if (!do_versions_match()) {
        return -1;
    }
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern("[%n] [%^%l%$] %v");
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("HailoRT CLI", console_sink));

    CLI::App app{"HailoRT CLI"};
    HailoRTCLI cli(&app);
    return cli.parse_and_execute(argc, argv);
}
