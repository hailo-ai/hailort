/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file benchmark_command.cpp
 * @brief measure basic performance on compiled network
 **/

#include "benchmark_command.hpp"
#include "hailortcli.hpp"
#include "infer_stats_printer.hpp"

#include <iostream>


BenchmarkCommand::BenchmarkCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("benchmark", "Measure basic performance on compiled network")),
    m_params({})
{
    add_device_options(m_app, m_params.device_params);
    add_vdevice_options(m_app, m_params.device_params);
    m_params.measure_overall_latency = false;
    m_params.power_measurement.measure_current = false;
    m_params.show_progress = true;
    m_params.transform.format_type = HAILO_FORMAT_TYPE_AUTO;

    m_app->add_option("hef", m_params.hef_path, "Path of the HEF to load")
        ->check(CLI::ExistingFile)
        ->required();
     m_app->add_option("-t, --time-to-run", m_time, "Measurement time in seconds per hw_only/streaming/latency measurement mode")
        ->check(CLI::PositiveNumber)
        ->default_val(15);
    m_app->add_option("--no-power", m_not_measure_power, "Skip power measurement, even if the platform supports it. The default value is False")
        ->default_val("false");
    m_app->add_option("--batch-size", m_params.batch_size, "Inference batch size (default is 1)")
        ->default_val(1);
    m_app->add_option("--input-files", m_params.inputs_name_and_file_path, "  The input files need to be in UINT8 before transformations.")
        ->check(InputNameToFileMap);
    m_app->add_option("--csv", m_csv_file_path, "If set print the output as csv to the specified path");
    
    auto measure_power_group = m_app->add_option_group("Measure Power");
    CLI::Option *power_sampling_period = measure_power_group->add_option("--sampling-period",
        m_params.power_measurement.sampling_period, "Sampling Period");
    CLI::Option *power_averaging_factor = measure_power_group->add_option("--averaging-factor",
        m_params.power_measurement.averaging_factor, "Averaging Factor");
    PowerMeasurementSubcommand::init_sampling_period_option(power_sampling_period);
    PowerMeasurementSubcommand::init_averaging_factor_option(power_averaging_factor);

    // TODO HRT-5363 support multiple devices
    m_app->parse_complete_callback([this]() {
        PARSE_CHECK((this->m_params.device_params.vdevice_params.device_count == 1) || this->m_csv_file_path.empty() || this->m_not_measure_power,
            "Writing power measurements in csv format is not supported for multiple devices");
    });
}

hailo_status BenchmarkCommand::execute()
{   
    std::cout << "Starting Measurements..." << std::endl;
    
    std::cout << "Measuring FPS in hw_only mode" << std::endl;
    auto hw_only_mode_info = hw_only_mode();
    CHECK_EXPECTED_AS_STATUS(hw_only_mode_info, "hw_only measuring failed");
    
    std::cout << "Measuring FPS " << (!m_not_measure_power ? "and Power " : "") << "in streaming mode" << std::endl; 
    auto streaming_mode_info = fps_streaming_mode();
    CHECK_EXPECTED_AS_STATUS(streaming_mode_info, "FPS in streaming mode failed");

    std::cout << "Measuring HW Latency" << std::endl;
    auto latency_info = latency();
    CHECK_EXPECTED_AS_STATUS(latency_info, "Latency measuring failed");

    std::cout << std::endl;
    std::cout << "=======" << std::endl;
    std::cout << "Summary" << std::endl;
    std::cout << "=======" << std::endl;
    std::cout << "FPS     (hw_only)                 = " << hw_only_mode_info->fps().value() <<std::endl;
    std::cout << "        (streaming)               = " << streaming_mode_info->fps().value() <<std::endl;
    if (auto hw_latency = latency_info->hw_latency()) {
        std::cout << "Latency (hw)                      = " << InferResultsFormatUtils::latency_result_to_ms(hw_latency.value()) << " ms" << std::endl;
    }
    if (auto overall_latency = latency_info->overall_latency()) {
        std::cout << "        (overall)                 = " << InferResultsFormatUtils::latency_result_to_ms(overall_latency.value()) << " ms" << std::endl;
    }
    if (!m_not_measure_power) {
        for (const auto &pair : streaming_mode_info->m_power_measurements) {
            std::cout << "Device " << pair.first << ":" << std::endl;
            const auto &data = pair.second->data();
            const auto &power_units = pair.second->power_units();
            std::cout << "  Power in streaming mode (average) = " << data.average_value << " " << power_units << std::endl;
            std::cout << "                          (max)     = " << data.max_value << " " << power_units << std::endl;
        }

    }

    if (!m_csv_file_path.empty()){
        m_params.csv_output = m_csv_file_path;
        auto printer = InferStatsPrinter::create(m_params, false);
        CHECK_EXPECTED_AS_STATUS(printer, "Failed to initialize infer stats printer");
        printer->print_benchmark_csv_header();
        printer->print_benchmark_csv(m_params.hef_path, hw_only_mode_info.release(), 
            streaming_mode_info.release(), latency_info.release());
    }
    return HAILO_SUCCESS;
}

Expected<NetworkGroupInferResult> BenchmarkCommand::hw_only_mode()
{
    m_params.transform.transform = (m_params.inputs_name_and_file_path.size() > 0);
    m_params.power_measurement.measure_power = false;
    m_params.measure_latency = false;
    m_params.mode = InferMode::HW_ONLY;
    m_params.time_to_run = m_time;
    return run_command_hef(m_params);
}

Expected<NetworkGroupInferResult> BenchmarkCommand::fps_streaming_mode()
{
    m_params.power_measurement.measure_power = !m_not_measure_power;
    m_params.mode = InferMode::STREAMING;
    m_params.measure_latency = false;
    m_params.transform.transform = true;
    m_params.transform.quantized = false;
    m_params.time_to_run = m_time;
    return run_command_hef(m_params);
}

Expected<NetworkGroupInferResult> BenchmarkCommand::latency()
{
    m_params.power_measurement.measure_power = false;
    m_params.measure_latency = true;
    m_params.mode = InferMode::STREAMING;
    m_params.transform.transform = true;
    m_params.transform.quantized = false;
    m_params.time_to_run = m_time;
    return run_command_hef(m_params);
}