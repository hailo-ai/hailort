/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailortcli.cpp
 * @brief HailoRT CLI.
 *
 * HailoRT command line interface.
 **/
#include "run2/run2_command.hpp"
#include "hailortcli.hpp"
#include "scan_command.hpp"
#include "power_measurement_command.hpp"
#include "run_command.hpp"
#include "fw_update_command.hpp"
#include "ssb_update_command.hpp"
#include "sensor_config_command.hpp"
#include "board_config_command.hpp"
#include "fw_config_command.hpp"
#include "benchmark_command.hpp"
#include "mon_command.hpp"
#include "logs_command.hpp"
#include "parse_hef_command.hpp"
#include "memory_requirements_command.hpp"
#include "fw_control_command.hpp"
#include "measure_nnc_performance_command.hpp"

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


#ifdef NDEBUG
#define LOGGER_PATTERN ("[%n] [%^%l%$] %v")
#else
#define LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v")
#endif

Expected<std::vector<std::string>> get_device_ids(const hailo_device_params &device_params)
{
    if (device_params.device_ids.empty()) {
        // No device id given, using all devices in the system.
        return Device::scan();
    }
    else {
        return std::vector<std::string>(device_params.device_ids);
    }
}

Expected<std::vector<std::unique_ptr<Device>>> create_devices(const hailo_device_params &device_params)
{
    std::vector<std::unique_ptr<Device>> res;

    TRY(const auto device_ids, get_device_ids(device_params));
    for (auto device_id : device_ids) {
        TRY(auto device, Device::create(device_id));
        res.emplace_back(std::move(device));
    }

    return res;
}

class BDFValidator : public CLI::Validator {
  public:
    BDFValidator() : Validator("BDF") {
        func_ = [](std::string &bdf) {
            auto pcie_device_info = Device::parse_pcie_device_info(bdf);
            if (pcie_device_info.has_value()) {
                return std::string();
            }
            else {
                return std::string("Invalid PCIe BDF " + bdf);
            }
        };
    }
};

void add_vdevice_options(CLI::App *app, hailo_vdevice_params &vdevice_params)
{
    add_device_options(app, vdevice_params.device_params);
    auto group = app->add_option_group("VDevice Options");
    auto device_count_option = group->add_option("--device-count", vdevice_params.device_count, "VDevice device count")
        ->check(CLI::PositiveNumber);
    group->add_flag("--multi-process-service", vdevice_params.multi_process_service,
        "VDevice multi process service");
    group->add_option("--group-id", vdevice_params.group_id, "VDevice group id");
    group->parse_complete_callback([&vdevice_params, device_count_option](){
        if (vdevice_params.device_params.device_ids.size() > 0) {
            // Check either device_count or device_id
            PARSE_CHECK(device_count_option->empty(),
                "Passing " + device_count_option->get_name() + " in combination with device-ids is not allowed");

            // Fill device_count with real value
            vdevice_params.device_count = static_cast<uint32_t>(vdevice_params.device_params.device_ids.size());
        }
    });
}

void add_device_options(CLI::App *app, hailo_device_params &device_params)
{
    auto group = app->add_option_group("Device Options");

    // General device id
    auto *device_id_option = group->add_option("-s,--device-id", device_params.device_ids,
        std::string("Device id, same as returned from `hailortcli scan` command. ") +
        std::string("For multiple devices, use space as separator.\n"));

    // PCIe options
    auto *pcie_bdf_option = group->add_option("--bdf", device_params.device_ids,
        std::string("Device bdf ([<domain>]:<bus>:<device>.<func>, same as in lspci command).\n") +
        std::string("For multiple BDFs, use space as separator.\n"))
        ->check(BDFValidator());

    // Ethernet options
    auto *ip_option = group->add_option("--ip", device_params.device_ids, "IP address of the target")
        ->check(CLI::ValidIPV4);

    group->parse_complete_callback([device_id_option, pcie_bdf_option, ip_option]()
    {
        // Check that only one device id param is given
        const std::string device_id_options_names = device_id_option->get_name(true, true) + ", " +
            pcie_bdf_option->get_name(true, true) + ", " +
            ip_option->get_name(true, true);

        const auto dev_id_options_parsed =
            static_cast<size_t>(!device_id_option->empty()) +
            static_cast<size_t>(!pcie_bdf_option->empty()) +
            static_cast<size_t>(!ip_option->empty());
        PARSE_CHECK(dev_id_options_parsed <= 1, 
            "Only one of " + device_id_options_names + " Can bet set");
    });
}

hailo_status validate_specific_device_is_given(const hailo_device_params &device_params)
{
    if ((1 != device_params.device_ids.size()) || contains(device_params.device_ids, std::string("*"))) {
        // No specific device-id given, make sure there is only 1 device on the machine.
        TRY(auto scan_res, Device::scan(), "Failed to scan for devices");
        CHECK(scan_res.size() > 0, HAILO_OUT_OF_PHYSICAL_DEVICES, "No devices found");
        CHECK(1 == scan_res.size(), HAILO_INVALID_OPERATION, "Multiple devices found, please specify a specific device-id/bdf/ip");
    }
    return HAILO_SUCCESS;
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
        m_app->set_version_flag("-v,--version", fmt::format("HailoRT-CLI version {}.{}.{}", HAILORT_MAJOR_VERSION, HAILORT_MINOR_VERSION, HAILORT_REVISION_VERSION));

        add_subcommand<RunCommand>();
        add_subcommand<Run2Command>();
        add_subcommand<ScanSubcommand>();
        add_subcommand<BenchmarkCommand>();
        add_subcommand<PowerMeasurementSubcommand>();
        add_subcommand<SensorConfigCommand>();
        add_subcommand<BoardConfigCommand>(OptionVisibility::HIDDEN);
        add_subcommand<FwConfigCommand>();
        add_subcommand<FwUpdateCommand>();
        add_subcommand<SSBUpdateCommand>();
        add_subcommand<MonCommand>();
#if defined(__GNUC__)
        add_subcommand<HwInferEstimatorCommand>(OptionVisibility::HIDDEN);
#endif
        add_subcommand<ParseHefCommand>();
        add_subcommand<MemoryRequirementsCommand>(OptionVisibility::HIDDEN);
        add_subcommand<FwControlCommand>();
        add_subcommand<LogsCommand>();
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
    console_sink->set_pattern(LOGGER_PATTERN);
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("HailoRT CLI", console_sink));

    CLI::App app{"HailoRT CLI"};
    HailoRTCLI cli(&app);
    return cli.parse_and_execute(argc, argv);
}
