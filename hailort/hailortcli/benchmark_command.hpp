/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file benchmarks_command.hpp
 * @brief measure basic performance on compiled network
 **/

#ifndef _HAILO_BENCHMARK_COMMAND_HPP_
#define _HAILO_BENCHMARK_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "run_command.hpp"
#include "CLI/CLI.hpp"

class BenchmarkCommand : public Command {
public:
    explicit BenchmarkCommand(CLI::App &parent_app);
    hailo_status execute() override;

private:
    Expected<InferResult> hw_only_mode();
    Expected<InferResult> fps_streaming_mode();
    Expected<InferResult> latency();

    inference_runner_params m_params;
    bool m_not_measure_power;
    std::string m_csv_file_path;
};

#endif /*_HAILO_BENCHMARK_COMMAND_HPP_*/
