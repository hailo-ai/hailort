/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file benchmark_command.cpp
 * @brief measure basic performance on compiled network
 **/

#include "benchmark_command.hpp"
#include "CLI/App.hpp"
#include "hailortcli.hpp"
#include "infer_stats_printer.hpp"
#include "run2/run2_command.hpp"

#include <iostream>


BenchmarkCommand::BenchmarkCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("benchmark", "Measure basic performance on compiled network")),
    m_params({})
{
    add_vdevice_options(m_app, m_params.vdevice_params);
    m_params.measure_overall_latency = false;
    m_params.power_measurement.measure_current = false;
    m_params.show_progress = true;
    m_params.transform.format_type = HAILO_FORMAT_TYPE_AUTO;

    m_app->add_option("hef", m_params.hef_path, "Path of the HEF to load")
        ->check(CLI::ExistingFile)
        ->required();
     m_app->add_option("-t, --time-to-run", m_params.time_to_run, "Measurement time in seconds per hw_only/streaming/latency measurement mode")
        ->check(CLI::PositiveNumber)
        ->default_val(15);
    m_app->add_option("--batch-size", m_params.batch_size, "Inference batch size (default is 1)")
        ->default_val(1);
    m_app->add_option("--power-mode", m_params.power_mode,
        "Core power mode (PCIE only; ignored otherwise)")
        ->transform(HailoCheckedTransformer<hailo_power_mode_t>({
            { "performance", hailo_power_mode_t::HAILO_POWER_MODE_PERFORMANCE },
            { "ultra_performance", hailo_power_mode_t::HAILO_POWER_MODE_ULTRA_PERFORMANCE }
        }))
        ->default_val("performance");
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
        PARSE_CHECK((m_params.vdevice_params.device_count == 1) || m_csv_file_path.empty() || m_not_measure_power,
            "Writing power measurements in csv format is not supported for multiple devices");
    });
}

hailo_status BenchmarkCommand::execute()
{
    // TODO - HRT-6931 - measure latency only in the case of single device.
    // TODO support overall-latency measurement (HRT-18357)
    // TODO support hardware only measurements (HRT-18357)
    // TODO support json and csv output (HRT-18357)
    return run2_benchmark(m_params.hef_path, m_params.time_to_run);
}

Expected<InferResult> BenchmarkCommand::hw_only_mode()
{
    m_params.transform.transform = (m_params.inputs_name_and_file_path.size() > 0);
    m_params.power_measurement.measure_power = ShouldMeasurePower::NO;
    m_params.measure_latency = false;
    m_params.mode = InferMode::HW_ONLY;
    return run_command_hef(m_params);
}

Expected<InferResult> BenchmarkCommand::fps_streaming_mode()
{
    m_params.power_measurement.measure_power = ShouldMeasurePower::AUTO_DETECT;
    m_params.mode = InferMode::STREAMING;
    m_params.measure_latency = false;
    m_params.transform.transform = true;
    m_params.transform.quantized = false;
    return run_command_hef(m_params);
}

Expected<InferResult> BenchmarkCommand::latency()
{
    m_params.power_measurement.measure_power = ShouldMeasurePower::NO;
    m_params.measure_latency = true;
    m_params.mode = InferMode::STREAMING;
    m_params.transform.transform = true;
    m_params.transform.quantized = false;
    return run_command_hef(m_params);
}
