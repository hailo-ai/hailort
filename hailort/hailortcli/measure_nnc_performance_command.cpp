/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file measure_nnc_performance_command.cpp
* @brief measure nerual network performance for given network using only the HW components without host SW
 **/

#include "measure_nnc_performance_command.hpp"
#include "hailortcli.hpp"

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hailo/hef.hpp"
#include "hailo/vstream.hpp"
#include "hailo/vdevice.hpp"

#include <iostream>

#define BYTES_TO_KILOBYTES (1024)

HwInferEstimatorCommand::HwInferEstimatorCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("measure-nnc-performance",
        "measure nerual network performance for given network using only the HW components without host SW")),
    m_params({})
{
    // This will make the command to be hidden in the --help print in the command line.
    m_app->group("");

    add_vdevice_options(m_app, m_params.vdevice_params);
    m_app->add_option("hef", m_params.hef_path, "Path of the HEF to load")
        ->check(CLI::ExistingFile)
        ->required();
    m_app->add_option("--batch-size", m_params.batch_size,
        "Inference batch.\n"
        "This batch applies to the whole network_group.")
        ->check(CLI::NonNegativeNumber)
        ->default_val(HAILO_DEFAULT_BATCH_SIZE);
}

Expected<std::map<std::string, ConfigureNetworkParams>> get_configure_params(const hw_infer_runner_params &params,
    hailort::Hef &hef, hailo_stream_interface_t interface)
{
    std::map<std::string, ConfigureNetworkParams> configure_params{};

    hailo_configure_params_t config_params{};
    hailo_status status = hailo_init_configure_params(reinterpret_cast<hailo_hef>(&hef), interface, &config_params);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* For default case overwrite batch to 1 */
    uint16_t batch_size = (HAILO_DEFAULT_BATCH_SIZE == params.batch_size ? 1 : params.batch_size);

    /* Fill all network and network group structs with batch size value */
    for (size_t network_group_idx = 0; network_group_idx < config_params.network_group_params_count; network_group_idx++) {
        config_params.network_group_params[network_group_idx].batch_size = batch_size;
    }

    for (size_t network_group_idx = 0; network_group_idx < config_params.network_group_params_count; network_group_idx++) {
        config_params.network_group_params[network_group_idx].power_mode = params.power_mode;
        configure_params.emplace(std::string(config_params.network_group_params[network_group_idx].name),
            ConfigureNetworkParams(config_params.network_group_params[network_group_idx]));
    }

    return configure_params;
}

hailo_status HwInferEstimatorCommand::execute()
{
    auto devices = create_devices(m_params.vdevice_params.device_params);
    CHECK_EXPECTED_AS_STATUS(devices, "Failed creating device");
    /* This function supports controls for multiple devices.
       We validate there is only 1 device generated as we are on a single device flow */
    CHECK(1 == devices->size(), HAILO_INTERNAL_FAILURE, "Hw infer command support only one physical device");
    auto &device = devices.value()[0];

    auto hef = Hef::create(m_params.hef_path.c_str());
    CHECK_EXPECTED_AS_STATUS(hef, "Failed reading hef file {}", m_params.hef_path);

    auto interface = device->get_default_streams_interface();
    CHECK_EXPECTED_AS_STATUS(interface, "Failed to get default streams interface");

    auto configure_params = get_configure_params(m_params, hef.value(), interface.value());
    CHECK_EXPECTED_AS_STATUS(configure_params);

    /* Use Env var to configure all desc list with max depth */
    setenv("HAILO_CONFIGURE_FOR_HW_INFER","Y",1);
    auto network_group_list = device->configure(hef.value(), configure_params.value());
    CHECK_EXPECTED_AS_STATUS(network_group_list, "Failed configure device from hef");
    unsetenv("HAILO_CONFIGURE_FOR_HW_INFER");

    CHECK(1 == network_group_list->size(), HAILO_INVALID_OPERATION,
        "HW Inference is not supported on HEFs with multiple network groups");

    auto network_group_ptr = network_group_list.value()[0];

    std::cout << "Starting HW infer Estimator..." << std::endl;

    auto results = network_group_ptr->run_hw_infer_estimator();
    CHECK_EXPECTED_AS_STATUS(results);

    std::cout << std::endl;
    std::cout << "======================" << std::endl;
    std::cout << "        Summary" << std::endl;
    std::cout << "======================" << std::endl;

    std::cout << "Batch count: " << results->batch_count << std::endl;
    std::cout << "Total transfer size [KB]: " << (results->total_transfer_size / BYTES_TO_KILOBYTES) << std::endl;
    std::cout << "Total frames passed: " << results->total_frames_passed << std::endl;
    std::cout << "Total time [s]: " << results->time_sec << std::endl;
    std::cout << "Total FPS [1/s]: " << results->fps << std::endl;
    std::cout << "BW [Gbps]: " << results->BW_Gbps << std::endl;

    std::cout << "======================" << std::endl;
    std::cout << "    End of report" << std::endl;
    std::cout << "======================" << std::endl;
    return HAILO_SUCCESS;
}
