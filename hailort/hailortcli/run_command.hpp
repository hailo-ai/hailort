/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file run_command.hpp
 * @brief Run inference on hailo device
 **/

#ifndef _HAILO_RUN_COMMAND_HPP_
#define _HAILO_RUN_COMMAND_HPP_

#include "hailortcli.hpp"
#include "common.hpp"
#include "power_measurement_command.hpp"
#include "common/device_measurements.hpp"
#include "CLI/CLI.hpp"
#include "inference_result.hpp"


enum class InferMode {
    STREAMING,
    HW_ONLY,
};

struct transformation_params {
    bool transform;
    bool quantized;
    hailo_format_type_t format_type;
};

struct measure_power_params {
    ShouldMeasurePower measure_power;
    bool measure_current;
    uint32_t sampling_period;
    uint32_t averaging_factor;
};

struct pipeline_stats_measurement_params {
    bool measure_elem_fps;
    bool measure_elem_latency;
    bool measure_elem_queue_size;
    bool measure_vstream_fps;
    bool measure_vstream_latency;
    std::string pipeline_stats_output_path;
};

struct runtime_data_params {
    bool collect_runtime_data;
    std::string runtime_data_output_path;
    std::string batch_to_measure_str;
    uint16_t batch_to_measure;
};

struct inference_runner_params {
    hailo_vdevice_params vdevice_params;
    std::string hef_path;
    uint32_t frames_count;
    uint32_t time_to_run;
    InferMode mode;
    std::string csv_output;
    std::vector<std::string> inputs_name_and_file_path;
    bool measure_latency;
    bool measure_overall_latency;
    bool show_progress;
    transformation_params transform;
    measure_power_params power_measurement;
    uint16_t batch_size;
    hailo_power_mode_t power_mode;
    pipeline_stats_measurement_params pipeline_stats;
    runtime_data_params runtime_data;
    std::string dot_output;
    bool measure_temp;
    std::vector<std::string> batch_per_network;
};

bool should_measure_pipeline_stats(const inference_runner_params& params);
CLI::App* create_run_command(CLI::App& parent, inference_runner_params& params);
hailo_status run_command(const inference_runner_params &params);
Expected<InferResult> run_command_hef(const inference_runner_params &params);

std::string format_type_to_string(hailo_format_type_t format_type);

class RunCommand : public Command {
public:
    explicit RunCommand(CLI::App &parent_app);
    hailo_status execute() override;

private:
    inference_runner_params m_params;
};

class UintOrKeywordValidator : public CLI::Validator {
public:
    UintOrKeywordValidator(const std::string &keyword) {
        name_ = "UINT_OR_KEYWORD";
        func_ = [keyword](const std::string &s) {
            if (s == keyword) {
                // s is the allowed keyword
                return std::string(); // Success
            }

            if (CliCommon::is_non_negative_number(s)) {
                // s is an int
                return std::string(); // Success
            }

            return std::string("should be a positive integer or '" + keyword + "'.");
        };
    }
};

class InputNameToFilePairValidator : public CLI::Validator {
public:
    InputNameToFilePairValidator() : CLI::Validator("InputNameToFilePair"), m_first_run(true), m_must_fail(false) {
        func_ = [this](std::string &key_value_pair_str) {
            bool old_first_run = m_first_run;
            m_first_run = false;
            if (m_must_fail) { // If a previous argument was not in key-value pair form, there shouldn't be more parameters
                return std::string("Parse failed at (" + key_value_pair_str + ")");
            }

            size_t first_delimiter = key_value_pair_str.find("=");
            if((std::string::npos == first_delimiter) || (key_value_pair_str.size() == first_delimiter + 1)) {
                if (old_first_run) { // We only accept non-key-value pair form if it's the very first parameter
                    m_must_fail = true;
                    return CLI::ExistingFile(key_value_pair_str);
                }
                return std::string("Failed parsing key-value pair: (" + key_value_pair_str + ")");
            }
            auto file_path = key_value_pair_str.substr(first_delimiter + 1);
            return CLI::ExistingFile(file_path);
        };

        desc_function_ = []() {
            return "\t\tInput file (.bin) path/paths. On single input network, give the full path of the data file.\n\
                    \t\tOn multiple inputs network, the format is input_name1=path1 input_name2=path2, where\n\
                    \t\tinput_name1 is the name of the input stream. If not given, random data will be used";
        };
    }
private:
    bool m_first_run;
    bool m_must_fail;
};

const static InputNameToFilePairValidator InputNameToFileMap;

class NetworkBatchValidator : public CLI::Validator {
public:
    NetworkBatchValidator() : CLI::Validator(), m_must_fail(false) {
        func_ = [this](std::string &key_value_pair_str) {
            if (m_must_fail) { // If a previous argument was not in a valid key-value pair form, there shouldn't be more parameters
                return std::string("Parse failed at (" + key_value_pair_str + ")");
            }

            size_t first_delimiter = key_value_pair_str.find("=");
            if((std::string::npos == first_delimiter) || (key_value_pair_str.size() == first_delimiter + 1)) {
                // We do not accept non-key-value pair form
                m_must_fail = true;
                return std::string("Failed parsing key-value pair: (" + key_value_pair_str + ")");
            }
            auto batch_size = key_value_pair_str.substr(first_delimiter + 1);
            auto network_name = key_value_pair_str.substr(0, first_delimiter);
            // Batch size must be a positive number
            if (!CliCommon::is_positive_number(batch_size)) {
                m_must_fail = true;
                return std::string("Failed parsing batch size (" + batch_size + ") for network (" + network_name + "). batch should be a positive number.");
            }
            return std::string("");
        };

    }

private:
    bool m_must_fail;
};

const static NetworkBatchValidator NetworkBatchMap;

#endif /* _HAILO_RUN_COMMAND_HPP_ */
