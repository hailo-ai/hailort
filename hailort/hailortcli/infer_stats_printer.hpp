/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_stats_printer.hpp
 * @brief Prints the inference stats
 **/

#ifndef _HAILO_INFER_STATS_PRINTER_HPP_
#define _HAILO_INFER_STATS_PRINTER_HPP_

#include "inference_result.hpp"
#include "run_command.hpp"

#include <limits>

class InferStatsPrinter final {
public:
    static double latency_result_to_ms(std::chrono::nanoseconds latency);
    static Expected<InferStatsPrinter> create(const inference_runner_params &params, bool print_running_info = true);
    void print(const std::vector<std::string> &network_groups_names, Expected<InferResult> &inference_result);
    void print_benchmark_csv(InferResult &hw_inference_result,
        InferResult &streaming_inference_result, InferResult &hw_latency_result);
    void print_csv_header();
    void print_benchmark_csv_header();

private:
    static constexpr uint32_t NO_INDEX = std::numeric_limits<uint32_t>::max();

    InferStatsPrinter(const inference_runner_params &params, hailo_status &output_status, bool print_running_info = true);
    void print_csv(const std::vector<std::string> &network_groups_names, Expected<InferResult> &inference_result);
    void print_pipeline_elem_stats_csv(const std::string &network_name,
        const std::map<std::string, std::map<std::string, AccumulatorPtr>> &inference_result);
    void print_pipeline_elem_stats_csv(const std::string &network_name,
        const std::map<std::string, std::map<std::string, std::vector<AccumulatorPtr>>> &inference_result);
    void print_entire_pipeline_stats_csv(const std::string &network_name,
        const std::map<std::string, AccumulatorPtr> &inference_result);
    void print_stdout(Expected<InferResult> &inference_result);
    template <typename T>
    void print_stdout_single_element(const T &results, size_t frames_count);

    // 'index' is only printed if it's not equal to 'NO_INDEX'
    static void write_accumulator_results(std::ofstream &output_stream, AccumulatorPtr accumulator,
        const std::string &network_name, const std::string &vstream_name, const std::string &elem_name,
        uint32_t index=NO_INDEX);

    std::ofstream m_results_csv_file;
    std::ofstream m_pipeline_stats_csv_file;
    std::string m_results_csv_path;
    std::string m_pipeline_stats_csv_path;
    std::string m_dot_output_path;
    bool m_print_frame_count;
};

#endif /* _HAILO_INFER_STATS_PRINTER_HPP_ */