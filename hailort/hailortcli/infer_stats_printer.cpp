/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_stats_printer.cpp
 * @brief Show inference progress
 **/

#include "infer_stats_printer.hpp"
#include "run_command.hpp"
#include "common.hpp"

#include "net_flow/pipeline/pipeline.hpp"

#include <fstream>
#include <iostream>
#include <sstream>


static std::string infer_mode_to_string(InferMode infer_mode)
{
    switch (infer_mode) {
    case InferMode::STREAMING:
        return "streaming";
    case InferMode::HW_ONLY:
        return "hw_only";
    default:
        return "???";
    }
}

double InferStatsPrinter::latency_result_to_ms(std::chrono::nanoseconds latency)
{
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(latency).count();
}

Expected<InferStatsPrinter> InferStatsPrinter::create(const inference_runner_params &params, bool print_running_info)
{
    hailo_status status = HAILO_UNINITIALIZED;
    InferStatsPrinter printer(params, status, print_running_info);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return printer;
}


InferStatsPrinter::InferStatsPrinter(const inference_runner_params &params, hailo_status &output_status, bool print_running_info) :
    m_print_frame_count(0 != params.time_to_run)
{
    if (!params.csv_output.empty()) {
        m_results_csv_path = params.csv_output;
        m_results_csv_file.open(params.csv_output.c_str(), std::ios::out);
        if (!m_results_csv_file.good()) {
            LOGGER__ERROR("Failed creating csv output file {}", params.csv_output);
            output_status = HAILO_OPEN_FILE_FAILURE;
            return;
        }
    }

    if (should_measure_pipeline_stats(params)) {
        m_pipeline_stats_csv_path = params.pipeline_stats.pipeline_stats_output_path;
        m_pipeline_stats_csv_file.open(params.pipeline_stats.pipeline_stats_output_path.c_str(),
            std::ios::out);
        if (!m_pipeline_stats_csv_file.good()) {
            LOGGER__ERROR("Failed creating pipeline stats csv output file {}",
                params.pipeline_stats.pipeline_stats_output_path);
            output_status = HAILO_OPEN_FILE_FAILURE;
            return;
        }
    }

    if (print_running_info) {
        std::cout << "Running " << infer_mode_to_string(params.mode) << " inference (" << params.hef_path << "):" << std::endl;
        std::cout << "  Transform data: " << std::boolalpha << params.transform.transform << std::endl;
        if (params.transform.transform) {
            std::cout << "    Type:      " << format_type_to_string(params.transform.format_type) << std::endl;
            std::cout << "    Quantized: " << std::boolalpha << params.transform.quantized << std::endl;
        }
    }

    if (!params.dot_output.empty()) {
        m_dot_output_path = params.dot_output;
    }

    output_status = HAILO_SUCCESS;
}

void InferStatsPrinter::print(const std::vector<std::string> &network_groups_names, Expected<InferResult> &inference_result)
{
    if (m_results_csv_file.is_open()) {
        std::cout << "> Writing inference results to '" << m_results_csv_path << "'... ";
        print_csv(network_groups_names, inference_result);
        std::cout << "done." << std::endl;
    }
    if (m_pipeline_stats_csv_file.is_open() && (inference_result)) {
        std::cout << "> Writing pipeline statistics to '" << m_pipeline_stats_csv_path << "'... ";
        m_pipeline_stats_csv_file << "net_name,vstream_name,param_type,element,mean,min,max,var,sd,mean_sd,index" << std::endl;
        for (auto &network_group_results : inference_result->network_group_results()) {
            print_pipeline_elem_stats_csv(network_group_results.network_group_name(), network_group_results.m_fps_accumulators);
            print_pipeline_elem_stats_csv(network_group_results.network_group_name(), network_group_results.m_latency_accumulators);
            print_pipeline_elem_stats_csv(network_group_results.network_group_name(), network_group_results.m_queue_size_accumulators);
            print_entire_pipeline_stats_csv(network_group_results.network_group_name(), network_group_results.m_pipeline_latency_accumulators);
        }
        std::cout << "done." << std::endl;
    }
    print_stdout(inference_result);
}

void InferStatsPrinter::print_csv_header()
{
    m_results_csv_file << "net_name,status,status_description,fps,num_of_frames,send_rate,recv_rate,hw_latency,overall_latency,min_power,average_power,max_power,min_current,average_current,max_current,min_temp,average_temp,max_temp" << std::endl;
}

void InferStatsPrinter::print_benchmark_csv_header()
{
    m_results_csv_file << "net_name,fps,hw_only_fps,num_of_frames,num_of_frames_hw_only,hw_latency,overall_latency,min_power,average_power,max_power" << std::endl;
}

void InferStatsPrinter::print_csv(const std::vector<std::string> &network_groups_names, Expected<InferResult> &inference_result)
{
    auto status_description = hailo_get_status_message(inference_result.status());
    if (HAILO_SUCCESS != inference_result.status()) {
        for (auto &network_group_name : network_groups_names) {
            m_results_csv_file << network_group_name << "," << static_cast<uint32_t>(inference_result.status()) << "," << status_description;
            if (!inference_result) {
                m_results_csv_file << ",,,,,,,,,,,";
            }
        }
    } else {
        for (auto &results : inference_result->network_group_results()) {
            m_results_csv_file << results.network_group_name() << "," << static_cast<uint32_t>(inference_result.status()) << "," << status_description;
            m_results_csv_file << ",";

            if (auto fps = results.fps()) {
                m_results_csv_file << fps.value();
            }
            m_results_csv_file << ",";

            if (auto frames_count = results.frames_count()) {
                m_results_csv_file << frames_count.value();
            }
            m_results_csv_file << ",";

            if (auto send_data_rate = results.send_data_rate_mbit_sec()) {
                m_results_csv_file << send_data_rate.value();
            }
            m_results_csv_file << ",";

            if (auto recv_data_rate = results.recv_data_rate_mbit_sec()) {
                m_results_csv_file << recv_data_rate.value();
            }
            m_results_csv_file << ",";

            if (auto hw_latency = results.hw_latency()) {
                m_results_csv_file << InferStatsPrinter::latency_result_to_ms(hw_latency.value());
            }
            m_results_csv_file << ",";

            if (auto overall_latency = results.overall_latency()) {
                m_results_csv_file << InferStatsPrinter::latency_result_to_ms(overall_latency.value());
            }

            // TODO HRT-5363 support multiple devices (Currently assumes 1 device in the map)
            if (1 == inference_result->m_power_measurements.size()) {
                for (const auto &pair : inference_result->m_power_measurements) {
                    if (nullptr != pair.second) {
                        m_results_csv_file << ",";
                        m_results_csv_file << pair.second->data().min_value;
                        m_results_csv_file << ",";
                        m_results_csv_file << pair.second->data().average_value;
                        m_results_csv_file << ",";
                        m_results_csv_file << pair.second->data().max_value;
                    } else {
                        m_results_csv_file << ",,,";
                    }
                }
            } else {
                m_results_csv_file << ",,,";
            }

            // TODO HRT-5363 support multiple devices (Currently assumes 1 device in the map)
            if (1 == inference_result->m_current_measurements.size()) {
                for (const auto &pair : inference_result->m_current_measurements) {
                    if (nullptr != pair.second) {
                        m_results_csv_file << ",";
                        m_results_csv_file << pair.second->data().min_value;
                        m_results_csv_file << ",";
                        m_results_csv_file << pair.second->data().average_value;
                        m_results_csv_file << ",";
                        m_results_csv_file << pair.second->data().max_value;
                    } else {
                        m_results_csv_file << ",,,";
                    }
                }
            } else {
                m_results_csv_file << ",,,";
            }

            // TODO HRT-5363 support multiple devices (Currently assumes 1 device in the map)
            if (1 == inference_result->m_temp_measurements.size()) {
                for (const auto &pair : inference_result->m_temp_measurements) {
                    if (nullptr != pair.second) {
                        m_results_csv_file << ",";
                        if (auto min = pair.second->min()) {
                            m_results_csv_file << *min;
                        }
                        m_results_csv_file << ",";
                        if (auto mean = pair.second->mean()) {
                            m_results_csv_file << *mean;
                        }
                        m_results_csv_file << ",";
                        if (auto max = pair.second->max()) {
                            m_results_csv_file << *max;
                        }
                    } else {
                        m_results_csv_file << ",,,";
                    }
                }
            } else {
                m_results_csv_file << ",,,";
            }
            m_results_csv_file << std::endl;
        }
    }
}

void InferStatsPrinter::print_pipeline_elem_stats_csv(const std::string &network_group_name,
    const std::map<std::string, std::map<std::string, AccumulatorPtr>> &inference_result)
{
    if (inference_result.size() == 0) {
        return;
    }

    for (const auto &vstream_name_results_pair : inference_result) {
        for (const auto &elem_name_accumulator_pair : vstream_name_results_pair.second) {
            write_accumulator_results(m_pipeline_stats_csv_file, elem_name_accumulator_pair.second, 
                network_group_name, vstream_name_results_pair.first, elem_name_accumulator_pair.first);
        }
    }
}

void InferStatsPrinter::print_pipeline_elem_stats_csv(const std::string &network_group_name,
    const std::map<std::string, std::map<std::string, std::vector<AccumulatorPtr>>> &inference_result)
{
    if (inference_result.size() == 0) {
        return;
    }

    for (const auto &vstream_name_results_pair : inference_result) {
        for (const auto &elem_name_accumulator_pair : vstream_name_results_pair.second) {
            for (uint32_t i = 0; i < elem_name_accumulator_pair.second.size(); i++) {
                write_accumulator_results(m_pipeline_stats_csv_file, elem_name_accumulator_pair.second[i], 
                    network_group_name, vstream_name_results_pair.first, elem_name_accumulator_pair.first, i);
            }
        }
    }
}

void InferStatsPrinter::print_entire_pipeline_stats_csv(const std::string &network_group_name,
    const std::map<std::string, AccumulatorPtr> &inference_result)
{
    if (inference_result.size() == 0) {
        return;
    }

    for (const auto &vstream_name_results_pair : inference_result) {
        write_accumulator_results(m_pipeline_stats_csv_file, vstream_name_results_pair.second, 
            network_group_name, vstream_name_results_pair.first, "entire_pipeline");
    }
}

void InferStatsPrinter::print_benchmark_csv(InferResult &hw_inference_result,
        InferResult &streaming_inference_result, InferResult &hw_latency_result)
{
    assert(hw_inference_result.network_group_results().size() == streaming_inference_result.network_group_results().size());
    assert(hw_latency_result.network_group_results().size() == streaming_inference_result.network_group_results().size());
    
    for (auto &hw_res : hw_inference_result.network_group_results()) {
        auto network_group_name = hw_res.network_group_name();

        auto streaming_res = std::find_if(streaming_inference_result.network_group_results().begin(), streaming_inference_result.network_group_results().end(),
            [network_group_name] (NetworkGroupInferResult &infer_results) { return (infer_results.network_group_name() == network_group_name); });

        auto latency_res = std::find_if(hw_latency_result.network_group_results().begin(), hw_latency_result.network_group_results().end(),
            [network_group_name] (NetworkGroupInferResult &infer_results) { return (infer_results.network_group_name() == network_group_name); });

        m_results_csv_file << network_group_name << ",";

        if (auto fps = streaming_res->fps()) {
            m_results_csv_file << fps.value();
        }
        m_results_csv_file << ",";

        if (auto hw_only_fps = hw_res.fps()) {
            m_results_csv_file << hw_only_fps.value();
        }
        m_results_csv_file << ",";

        if (auto frames_count = streaming_res->frames_count()) {
            m_results_csv_file << frames_count.value();
        }
        m_results_csv_file << ",";

        if (auto frames_count = hw_res.frames_count()) {
            m_results_csv_file << frames_count.value();
        }
        m_results_csv_file << ",";

        if (auto hw_latency = latency_res->hw_latency()) {
            m_results_csv_file << InferStatsPrinter::latency_result_to_ms(hw_latency.value());
        }
        m_results_csv_file << ",";

        if (auto overall_latency = latency_res->overall_latency()) {
            m_results_csv_file << InferStatsPrinter::latency_result_to_ms(overall_latency.value());
        }

        // TODO HRT-5363 support multiple devices (Currently assumes 1 device in the map)
        if (1 == streaming_inference_result.m_power_measurements.size()) {
            for (const auto &pair : streaming_inference_result.m_power_measurements) {
                if (nullptr != pair.second) {
                    m_results_csv_file << ",";
                    m_results_csv_file << pair.second->data().min_value;
                    m_results_csv_file << ",";
                    m_results_csv_file << pair.second->data().average_value;
                    m_results_csv_file << ",";
                    m_results_csv_file << pair.second->data().max_value;
                } else {
                    m_results_csv_file << ",,,";
                }
            }
        } else {
            m_results_csv_file << ",,,";
        }

        m_results_csv_file << std::endl;
    }
}
template< typename T>
void InferStatsPrinter::print_stdout_single_element(const T &results, size_t frames_count)
{
    if (0 != frames_count) {
        std::cout << "    Frames count: " << static_cast<uint32_t>(frames_count) << std::endl;
    } else if (auto duration = results.infer_duration()) {
        std::cout << "    Duration: " << CliCommon::duration_to_string(std::chrono::seconds(static_cast<uint32_t>(*duration))) << std::endl;
    }

    if (auto fps = results.fps()) {
        std::cout << "    FPS: " << fps.value() << "" << std::endl;
    }

    if (auto send_data_rate = results.send_data_rate_mbit_sec()) {
        std::cout << "    Send Rate: " << send_data_rate.value() << " Mbit/s" << std::endl;
    }

    if (auto recv_data_rate = results.recv_data_rate_mbit_sec()) {
        std::cout << "    Recv Rate: " << recv_data_rate.value() << " Mbit/s" << std::endl;
    }

    if (auto hw_latency = results.hw_latency()) {
        std::cout << "    HW Latency: " << InferStatsPrinter::latency_result_to_ms(hw_latency.value()) << " ms" << std::endl;
    }

    if (auto overall_latency = results.overall_latency()) {
        std::cout << "    Overall Latency: " << InferStatsPrinter::latency_result_to_ms(overall_latency.value()) << " ms" << std::endl;
    }

}

void InferStatsPrinter::print_stdout(Expected<InferResult> &inference_result)
{
    if (!inference_result) {
        return;
    }

    // Set precision and flags
    auto original_precision = std::cout.precision();
    auto original_flags(std::cout.flags());
    std::cout << std::setprecision(2) << std::fixed;
    std::cout  << FORMAT_CLEAR_LINE << "> Inference result:" << std::endl;

    for (auto &network_group_results : inference_result.value().network_group_results()) {
        std::cout << " Network group: " << network_group_results.network_group_name() <<  std::endl;
        if (1 < network_group_results.m_result_per_network.size()) {
            // If there is more than 1 network, we print results per network, and than sum of bandwith
            for (auto &network_result_pair : network_group_results.m_result_per_network) {
                std::cout << "  Network: " << network_result_pair.first <<  std::endl;
                auto frames_count = (m_print_frame_count) ? network_result_pair.second.m_frames_count : 0;
                print_stdout_single_element<NetworkInferResult>(network_result_pair.second, frames_count);
            }
            std::stringstream bandwidth_stream;
            bandwidth_stream << std::setprecision(2) << std::fixed;
            if (auto send_data_rate = network_group_results.send_data_rate_mbit_sec()) {
                bandwidth_stream << "    Send Rate: " << send_data_rate.value() << " Mbit/s" << std::endl;
            }

            if (auto recv_data_rate = network_group_results.recv_data_rate_mbit_sec()) {
                bandwidth_stream << "    Recv Rate: " << recv_data_rate.value() << " Mbit/s" << std::endl;
            }

            if (0 != bandwidth_stream.rdbuf()->in_avail()) {
                std::cout << "  Total bandwidth: " <<  std::endl;
                std::cout << bandwidth_stream.rdbuf();
            }
            std::cout << std::endl;
        } else {
            auto frames_count_exp = network_group_results.frames_count();
            auto frames_count = ((frames_count_exp) && (m_print_frame_count)) ? frames_count_exp.value() : 0;
            print_stdout_single_element<NetworkGroupInferResult>(network_group_results, frames_count);
            std::cout << std::endl;
        }
    }

    if ((inference_result->m_power_measurements.size() != inference_result->m_current_measurements.size()) ||
            (inference_result->m_power_measurements.size() != inference_result->m_temp_measurements.size())) {
        LOGGER__ERROR("Error found different number of devices between different measurement types");
    }
    for (const auto &pair : inference_result->m_power_measurements) {
        std::stringstream measurement_stream;
        if (nullptr != pair.second) {
            const auto &data = pair.second->data();
            const auto &power_units = pair.second->power_units();
            measurement_stream << "    Minimum power consumption: " << data.min_value << " " << power_units << std::endl;
            measurement_stream << "    Average power consumption: " << data.average_value << " " << power_units << std::endl;
            measurement_stream << "    Maximum power consumption: " << data.max_value << " " << power_units << std::endl;
        }
        auto current_measure_iter = inference_result->m_current_measurements.find(pair.first);
        if ((current_measure_iter != inference_result->m_current_measurements.end()) && (nullptr != current_measure_iter->second)) {
            const auto &data = current_measure_iter->second->data();
            const auto &power_units = current_measure_iter->second->power_units();
            measurement_stream << "    Minimum current consumption: " << data.min_value << " " << power_units << std::endl;
            measurement_stream << "    Average current consumption: " << data.average_value << " " << power_units << std::endl;
            measurement_stream << "    Maximum current consumption: " << data.max_value << " " << power_units << std::endl;
        }
        auto temp_measure_iter = inference_result->m_temp_measurements.find(pair.first);
        if ((temp_measure_iter != inference_result->m_temp_measurements.end()) && (nullptr != temp_measure_iter->second)) {
            if (auto min = temp_measure_iter->second->min()) {
                measurement_stream << "    Minimum chip temperature: " << *min << "C" << std::endl;
            }
            if (auto mean = temp_measure_iter->second->mean()) {
                measurement_stream << "    Average chip temperature: " << *mean << "C" << std::endl;
            }
            if (auto max = temp_measure_iter->second->max()) {
                measurement_stream << "    Maximum chip temperature: " << *max << "C" << std::endl;
            }
        }
        if (0 != measurement_stream.rdbuf()->in_avail()) {
            std::cout << "  Device: " << pair.first << std::endl;
            std::cout << measurement_stream.rdbuf();
        }
    }

    // Restore precision and flags
    std::cout.flags(original_flags);
    std::cout.precision(original_precision);
}

void InferStatsPrinter::write_accumulator_results(std::ofstream &output_stream, AccumulatorPtr accumulator,
    const std::string &network_group_name, const std::string &vstream_name, const std::string &elem_name, uint32_t index)
{
    const auto &accumulator_result = accumulator->get();
    if ((!accumulator_result.count()) || (accumulator_result.count().value() == 0)) {
        LOGGER__WARNING("No {} data has been collected for element '{}' (vstream '{}'). Collection begins after the element has processed {} frames...",
            accumulator->get_data_type(), elem_name, vstream_name, DEFAULT_NUM_FRAMES_BEFORE_COLLECTION_START);
        return;
    }
    
    output_stream << network_group_name << ",";
    output_stream << vstream_name << ",";
    output_stream << accumulator->get_data_type() << ",";
    output_stream << elem_name << ",";
    output_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.mean()) << ",";
    output_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.min()) << ",";
    output_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.max()) << ",";
    output_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.var()) << ",";
    output_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.sd()) << ",";
    output_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.mean_sd()) << ",";
    if (NO_INDEX != index) {
        output_stream << index;
    }
    output_stream << std::endl;
}
