/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file run_command.cpp
 * @brief Run inference on hailo device
 **/

#include "run_command.hpp"
#include "hailortcli.hpp"
#include "inference_progress.hpp"
#include "infer_stats_printer.hpp"
#include "graph_printer.hpp"
#include "download_action_list_command.hpp"
#include "common.hpp"
#include "buffer_utils.hpp"

#include "common/utils.hpp"
#include "common/file_utils.hpp"
#include "common/async_thread.hpp"
#include "common/barrier.hpp"
#include "common/latency_meter.hpp"
#include "common/filesystem.hpp"
#include "common/device_measurements.hpp"
#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hailo/hef.hpp"
#include "hailo/vstream.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/transform.hpp"

#include "spdlog/fmt/fmt.h"

#include <vector>
#include <algorithm>
#include <regex>
#include <signal.h>
#include <condition_variable>
std::condition_variable wait_for_exit_cv;

/* The SIGUSR1 and SIGUSR2 signals are set aside for you to use any way you want.
    They're useful for simple interprocess communication. */
#define USER_SIGNAL (SIGUSR1)

constexpr uint32_t DEFAULT_TIME_TO_RUN_SECONDS = 5;
#ifndef HAILO_EMULATOR
#define HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS (HAILO_DEFAULT_VSTREAM_TIMEOUT_MS)
#else /* ifndef HAILO_EMULATOR */
#define HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS (HAILO_DEFAULT_VSTREAM_TIMEOUT_MS * 100)
#endif /* ifndef HAILO_EMULATOR */
static const char *RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER = "<hef>";
static const char *RUNTIME_DATA_BATCH_TO_MEASURE_OPT_LAST = "last";
static const char *RUNTIME_DATA_BATCH_TO_MEASURE_OPT_DEFAULT = "2";

#ifndef _MSC_VER
void user_signal_handler_func(int signum)
{
    if (USER_SIGNAL == signum)
    {
        wait_for_exit_cv.notify_one();
    }
}
#endif

hailo_status wait_for_exit_with_timeout(std::chrono::seconds time_to_run)
{
#if defined(__linux__)
    sighandler_t prev_handler = signal(USER_SIGNAL, user_signal_handler_func);
    CHECK(prev_handler != SIG_ERR, HAILO_INVALID_OPERATION, "signal failed, errno = {}", errno);
    std::mutex mutex;
    std::unique_lock<std::mutex> condition_variable_lock(mutex);
    wait_for_exit_cv.wait_for(condition_variable_lock, time_to_run);
#else
    std::this_thread::sleep_for(time_to_run);
#endif
    return HAILO_SUCCESS;
}

bool should_measure_pipeline_stats(const inference_runner_params& params)
{
    const std::vector<bool> measure_flags = { params.pipeline_stats.measure_elem_fps,
        params.pipeline_stats.measure_elem_latency, params.pipeline_stats.measure_elem_queue_size,
        params.pipeline_stats.measure_vstream_fps, params.pipeline_stats.measure_vstream_latency
    };

    return std::any_of(measure_flags.cbegin(), measure_flags.cend(), [](bool x){ return x; });
}

bool use_batch_to_measure_opt(const inference_runner_params& params)
{
    return params.runtime_data.collect_runtime_data &&
        (params.runtime_data.batch_to_measure_str != RUNTIME_DATA_BATCH_TO_MEASURE_OPT_LAST);
}

// We assume that hef_place_holder_regex is valid
std::string format_runtime_data_output_path(const std::string &base_output_path, const std::string &hef_path,
    const std::string &hef_place_holder_regex = RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER,
    const std::string &hef_suffix = ".hef")
{
    const auto hef_basename = Filesystem::basename(hef_path);
    const auto hef_no_suffix = Filesystem::remove_suffix(hef_basename, hef_suffix);
    return std::regex_replace(base_output_path, std::regex(hef_place_holder_regex), hef_no_suffix);
}

static void add_run_command_params(CLI::App *run_subcommand, inference_runner_params& params)
{
    // TODO: init values in RunCommand ctor
    params.measure_latency = false;
    params.measure_overall_latency = false;
    params.power_measurement.measure_power = ShouldMeasurePower::NO;
    params.power_measurement.measure_current = false;
    params.show_progress = true;
    params.time_to_run = 0;
    params.frames_count = 0;
    params.measure_temp = false;

    add_vdevice_options(run_subcommand, params.vdevice_params);

    auto hef_new = run_subcommand->add_option("hef", params.hef_path, "An existing HEF file/directory path")
        ->check(CLI::ExistingFile | CLI::ExistingDirectory);

    // Allow multiple subcommands (see https://cliutils.github.io/CLI11/book/chapters/subcommands.html)
    run_subcommand->require_subcommand(0, 0);

    CLI::Option *frames_count = run_subcommand->add_option("-c,--frames-count", params.frames_count,
        "Frames count to run")
        ->check(CLI::PositiveNumber);
    run_subcommand->add_option("-t,--time-to-run", params.time_to_run, "Time to run (seconds)")
        ->check(CLI::PositiveNumber)
        ->excludes(frames_count);
    auto total_batch_size = run_subcommand->add_option("--batch-size", params.batch_size,
        "Inference batch (should be a divisor of --frames-count if provided).\n"
        "This batch applies to the whole network_group. for differential batch per network, see --net-batch-size.\n"
        "The default value is HAILO_DEFAULT_BATCH_SIZE - which means the batch is determined by HailoRT automatically")
        ->check(CLI::NonNegativeNumber)
        ->default_val(HAILO_DEFAULT_BATCH_SIZE);

    run_subcommand->add_option("--net-batch-size", params.batch_per_network,
        "Inference batch per network (network names can be found using parse-hef command).\n"
        "In case of multiple networks, usage is as follows: --net-batch-size <network_name_1>=<batch_size_1> --net-batch-size <network_name_2>=<batch_size_2>")
        ->check(NetworkBatchMap)
        ->excludes(total_batch_size)
        ->excludes(frames_count);

    CLI::Option *power_mode_option = run_subcommand->add_option("--power-mode", params.power_mode,
        "Core power mode (PCIE only; ignored otherwise)")
        ->transform(HailoCheckedTransformer<hailo_power_mode_t>({
            { "performance", hailo_power_mode_t::HAILO_POWER_MODE_PERFORMANCE },
            { "ultra_performance", hailo_power_mode_t::HAILO_POWER_MODE_ULTRA_PERFORMANCE }
        }))
        ->default_val("performance");
    run_subcommand->add_option("-m,--mode", params.mode, "Inference mode")
        ->transform(HailoCheckedTransformer<InferMode>({
            { "streaming", InferMode::STREAMING },
            { "hw_only", InferMode::HW_ONLY }
        }))
        ->default_val("streaming");
    run_subcommand->add_option("--csv", params.csv_output, "If set print the output as csv to the specified path");
    run_subcommand->add_option("--input-files", params.inputs_name_and_file_path)
        ->check(InputNameToFileMap);
    run_subcommand->add_flag("--measure-latency", params.measure_latency,
        "Measure network latency");
    run_subcommand->add_flag("--measure-overall-latency", params.measure_overall_latency,
        "Include overall latency measurement")
        ->needs("--measure-latency");

    static const char *DOT_SUFFIX = ".dot";
    run_subcommand->add_option("--dot", params.dot_output,
        "If set print the pipeline graph as a .dot file at the specified path")
        ->check(FileSuffixValidator(DOT_SUFFIX));
    auto measure_power_cb = [&params] (bool measure_power) { params.power_measurement.measure_power = measure_power ? ShouldMeasurePower::YES : ShouldMeasurePower::NO; };
    CLI::Option *measure_power_opt = run_subcommand->add_flag( "--measure-power", measure_power_cb, "Measure power consumption");
    CLI::Option *measure_current_opt = run_subcommand->add_flag("--measure-current",
        params.power_measurement.measure_current, "Measure current")->excludes(measure_power_opt);
    measure_power_opt->excludes(measure_current_opt);

    run_subcommand->add_flag("--show-progress,!--dont-show-progress", params.show_progress,
        "Show inference progress")
        ->default_val("true");

    auto transformation_group = run_subcommand->add_option_group("Transformations");
    auto quantized_option = transformation_group->add_option("--quantized", params.transform.quantized,
        "true means the tool assumes that the data is already quantized,\n"
        "false means it is the tool's responsibility to quantize (scale) the data.")
        ->default_val("true");
    transformation_group->add_option("--user-format-type", params.transform.format_type,
        "The host data type")
        ->transform(HailoCheckedTransformer<hailo_format_type_t>({
            { "auto", HAILO_FORMAT_TYPE_AUTO },
            { "uint8", HAILO_FORMAT_TYPE_UINT8 },
            { "uint16", HAILO_FORMAT_TYPE_UINT16 },
            { "float32", HAILO_FORMAT_TYPE_FLOAT32 }
        }))
        ->default_val("auto");

    auto *measure_stats_subcommand = run_subcommand->add_subcommand("measure-stats", "Pipeline Statistics Measurements");
    CLI::Option *elem_fps_option = measure_stats_subcommand->add_flag("--elem-fps", params.pipeline_stats.measure_elem_fps,
        "Measure the fps of each pipeline element separately");
    CLI::Option *elem_latency_option = measure_stats_subcommand->add_flag("--elem-latency", params.pipeline_stats.measure_elem_latency,
        "Measure the latency of each pipeline element separately")
        ->group(""); // --elem-latency will be hidden in the --help print.
    CLI::Option *elem_queue_size_option = measure_stats_subcommand->add_flag("--elem-queue-size", params.pipeline_stats.measure_elem_queue_size,
        "Measure the queue size of each pipeline element separately");

    // TODO (HRT-4522): Remove comment-out
    // measure_stats_subcommand->add_flag("--vstream-fps", params.pipeline_stats.measure_vstream_fps,
    //     "Measure the fps of the entire vstream pipeline");

    measure_stats_subcommand->add_flag("--vstream-latency", params.pipeline_stats.measure_vstream_latency,
        "Measure the latency of the entire vstream pipeline")
        ->group(""); // --vstream-latency will be hidden in the --help print.
    measure_stats_subcommand->add_option("--output-path", params.pipeline_stats.pipeline_stats_output_path,
        "Path to a '.csv' file that will contain the measured pipeline statistics")
        ->default_val("pipeline_stats.csv");
    measure_stats_subcommand->parse_complete_callback([&params]() {
        PARSE_CHECK(should_measure_pipeline_stats(params),
            "No measurement flags provided; Run 'hailortcli run measure-stats --help' for options");
    });

    auto *collect_runtime_data_subcommand = run_subcommand->add_subcommand("collect-runtime-data",
        "Collect runtime data to be used by the Profiler");
    static const char *JSON_SUFFIX = ".json";
    collect_runtime_data_subcommand->add_option("--output-path", params.runtime_data.runtime_data_output_path,
        fmt::format("Runtime data output file path\n'{}' will be replaced with the current running hef", RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER)
         + "\nIn case of multiple-devices, <device-id>_ will be added as prefix to each file")
        ->default_val(fmt::format("runtime_data_{}.json", RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER))
        ->check(FileSuffixValidator(JSON_SUFFIX));
    collect_runtime_data_subcommand->add_option("--batch-to-measure", params.runtime_data.batch_to_measure_str,
        fmt::format("Batch to be measured (non-negative integer)\nThe last batch will be measured if '{}' is provided",
            RUNTIME_DATA_BATCH_TO_MEASURE_OPT_LAST))
        ->default_val(RUNTIME_DATA_BATCH_TO_MEASURE_OPT_DEFAULT)
        ->check(UintOrKeywordValidator(RUNTIME_DATA_BATCH_TO_MEASURE_OPT_LAST));
    collect_runtime_data_subcommand->parse_complete_callback([&params]() {
        // If this subcommand was parsed, then we need to download runtime_data
        params.runtime_data.collect_runtime_data = true;
    });

    auto measure_power_group = run_subcommand->add_option_group("Measure Power/Current");
    CLI::Option *power_sampling_period = measure_power_group->add_option("--sampling-period",
        params.power_measurement.sampling_period, "Sampling Period");
    CLI::Option *power_averaging_factor = measure_power_group->add_option("--averaging-factor",
        params.power_measurement.averaging_factor, "Averaging Factor");
    PowerMeasurementSubcommand::init_sampling_period_option(power_sampling_period);
    PowerMeasurementSubcommand::init_averaging_factor_option(power_averaging_factor);

    CLI::Option *measure_temp_option = run_subcommand->add_flag("--measure-temp", params.measure_temp, "Measure chip temperature");

    CLI::Option *multi_process_option = run_subcommand->get_option("--multi-process-service");
    multi_process_option->excludes(measure_power_opt)
                        ->excludes(measure_current_opt)
                        ->excludes(power_mode_option)
                        ->excludes(power_sampling_period)
                        ->excludes(power_averaging_factor)
                        ->excludes(measure_temp_option)
                        ->excludes(elem_fps_option)
                        ->excludes(elem_latency_option)
                        ->excludes(elem_queue_size_option);

    hailo_deprecate_options(run_subcommand, { std::make_shared<OptionDeprecation>(quantized_option) }, false);

    run_subcommand->parse_complete_callback([&params, hef_new, power_sampling_period,
            power_averaging_factor, measure_power_opt, measure_current_opt]() {
        PARSE_CHECK(!hef_new->empty(), "Single HEF file/directory is required");
        bool is_hw_only = InferMode::HW_ONLY == params.mode;
        params.transform.transform = (!is_hw_only || (params.inputs_name_and_file_path.size() > 0));
        bool has_oneof_measure_flags = (!measure_power_opt->empty() || !measure_current_opt->empty());
        PARSE_CHECK(power_sampling_period->empty() || has_oneof_measure_flags,
            "--sampling-period requires --measure-power or --measure-current");
        PARSE_CHECK(power_averaging_factor->empty() || has_oneof_measure_flags,
            "--averaging-period factor --measure-power or --measure-current");
        PARSE_CHECK(((0 != params.time_to_run) || (HAILO_DEFAULT_BATCH_SIZE == params.batch_size) || (0 == (params.frames_count % params.batch_size))),
            "--batch-size should be a divisor of --frames-count if provided");
        // TODO HRT-5363 support multiple devices
        PARSE_CHECK((params.vdevice_params.device_count == 1) || params.csv_output.empty() ||
            !((ShouldMeasurePower::YES == params.power_measurement.measure_power) || params.power_measurement.measure_current || params.measure_temp),
            "Writing measurements in csv format is not supported for multiple devices");

        if ((0 == params.time_to_run) && (0 == params.frames_count)) {
            // Use default
            params.time_to_run = DEFAULT_TIME_TO_RUN_SECONDS;
        }

        PARSE_CHECK((!(params.runtime_data.collect_runtime_data && params.vdevice_params.multi_process_service)),
            "Passing runtime data is not supported for multi process service");

        PARSE_CHECK(!(params.vdevice_params.multi_process_service && is_hw_only),
            "--hw-only mode is not supported for multi process service");

        if (use_batch_to_measure_opt(params)) {
            // This cast is ok because we validate params.runtime_data.batch_to_measure_str with UintOrKeywordValidator
            params.runtime_data.batch_to_measure = static_cast<uint16_t>(std::stoi(params.runtime_data.batch_to_measure_str));
            if ((0 != params.frames_count) && (params.frames_count < params.runtime_data.batch_to_measure)) {
                LOGGER__WARNING("--frames-count ({}) is smaller than --batch-to-measure ({}), "
                    "hence timestamps will not be updated in runtime data", params.frames_count,
                    params.runtime_data.batch_to_measure);
            }
        }

        PARSE_CHECK((params.dot_output.empty() || !is_hw_only),
            "Generating .dot file for pipeline graph is impossible when running in 'hw-only' mode");

    });
}

std::map<std::string, std::string> format_strings_to_key_value_pairs(const std::vector<std::string> &key_value_pairs_str) {
    std::map<std::string, std::string> pairs = {};
    for (const auto &key_value_pair_str : key_value_pairs_str) {
        size_t first_delimiter = key_value_pair_str.find("=");
        auto key = key_value_pair_str.substr(0, first_delimiter);
        auto file_path = key_value_pair_str.substr(first_delimiter + 1);
        pairs.emplace(key, file_path);
    }
    return pairs;
}

std::string format_type_to_string(hailo_format_type_t format_type) {
    switch (format_type) {
    case HAILO_FORMAT_TYPE_AUTO:
        return "auto";
    case HAILO_FORMAT_TYPE_UINT8:
        return "uint8";
    case HAILO_FORMAT_TYPE_UINT16:
        return "uint16";
    case HAILO_FORMAT_TYPE_FLOAT32:
        return "float32";
    default:
        return "<INVALID_TYPE>";
    }
}

static hailo_vstream_stats_flags_t inference_runner_params_to_vstream_stats_flags(
    const pipeline_stats_measurement_params &params)
{
    hailo_vstream_stats_flags_t result = HAILO_VSTREAM_STATS_NONE;
    if (params.measure_vstream_fps) {
        result |= HAILO_VSTREAM_STATS_MEASURE_FPS;
    }
    if (params.measure_vstream_latency) {
        result |= HAILO_VSTREAM_STATS_MEASURE_LATENCY;
    }

    return result;
}

static hailo_pipeline_elem_stats_flags_t inference_runner_params_to_pipeline_elem_stats_flags(
    const pipeline_stats_measurement_params &params)
{
    hailo_pipeline_elem_stats_flags_t result = HAILO_PIPELINE_ELEM_STATS_NONE;
    if (params.measure_elem_fps) {
        result |= HAILO_PIPELINE_ELEM_STATS_MEASURE_FPS;
    }
    if (params.measure_elem_latency) {
        result |= HAILO_PIPELINE_ELEM_STATS_MEASURE_LATENCY;
    }
    if (params.measure_elem_queue_size) {
        result |= HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE;
    }

    return result;
}

static size_t total_send_frame_size(const std::vector<std::reference_wrapper<InputStream>> &input_streams)
{
    size_t total_send_frame_size = 0;
    for (const auto &input_stream : input_streams) {
        total_send_frame_size += input_stream.get().get_frame_size();
    }
    return total_send_frame_size;
}

static size_t total_recv_frame_size(const std::vector<std::reference_wrapper<OutputStream>> &output_streams)
{
    size_t total_recv_frame_size = 0;
    for (const auto &output_stream : output_streams) {
        total_recv_frame_size += output_stream.get().get_frame_size();
    }
    return total_recv_frame_size;
}

template<typename SendObject>
hailo_status send_loop(const inference_runner_params &params, SendObject &send_object,
    const std::map<std::string, BufferPtr> &input_dataset, Barrier &barrier, LatencyMeter &overall_latency_meter, uint16_t batch_size)
{
    assert(input_dataset.find(send_object.name()) != input_dataset.end());
    const BufferPtr &input_buffer = input_dataset.at(send_object.name());
    assert((input_buffer->size() % send_object.get_frame_size()) == 0);
    const size_t frames_in_buffer = input_buffer->size() / send_object.get_frame_size();
    // TODO: pass the correct batch (may be different between networks)
    uint32_t num_of_batches = (0 == params.time_to_run ? (params.frames_count / batch_size) : UINT32_MAX);
    for (uint32_t i = 0; i < num_of_batches; i++) {
        if (params.measure_latency) {
            barrier.arrive_and_wait();
        }
        for (int j = 0; j < batch_size; j++) {
            if (params.measure_overall_latency) {
                overall_latency_meter.add_start_sample(std::chrono::steady_clock::now().time_since_epoch());
            }

            const size_t offset = (i % frames_in_buffer) * send_object.get_frame_size();
            auto status = send_object.write(MemoryView(
                const_cast<uint8_t*>(input_buffer->data()) + offset,
                send_object.get_frame_size()));
            if (HAILO_STREAM_ABORT == status) {
                LOGGER__DEBUG("Input stream was aborted!");
                return status;
            }
            CHECK_SUCCESS(status);
        }
    }
    // Flushing the send object in order to make sure all data is sent. Needed for latency measurement as well.
    auto status = send_object.flush();
    CHECK_SUCCESS(status, "Failed flushing stream");
    return HAILO_SUCCESS;
}

template<typename RecvObject>
hailo_status recv_loop(const inference_runner_params &params, RecvObject &recv_object,
    std::shared_ptr<NetworkProgressBar> progress_bar, Barrier &barrier, LatencyMeter &overall_latency_meter,
    std::map<std::string, BufferPtr> &dst_data, std::atomic_size_t &received_frames_count, bool show_progress,
    uint16_t batch_size)
{
    uint32_t num_of_batches = ((0 == params.time_to_run) ? (params.frames_count / batch_size) : UINT32_MAX);
    for (size_t i = 0; i < num_of_batches; i++) {
        if (params.measure_latency) {
            barrier.arrive_and_wait();
        }
        for (int j = 0; j < batch_size; j++) {
            auto dst_buffer = MemoryView(*dst_data[recv_object.name()]);
            auto status = recv_object.read(dst_buffer);
            if (HAILO_SUCCESS != status) {
                return status;
            }

            if (params.measure_overall_latency) {
                overall_latency_meter.add_end_sample(recv_object.name(), std::chrono::steady_clock::now().time_since_epoch());
            }

            if (show_progress && params.show_progress) {
                progress_bar->make_progress();
            }
            received_frames_count++;
        }
    }
    return HAILO_SUCCESS;
}

Expected<std::map<std::string, std::vector<InputVStream>>> create_input_vstreams(ConfiguredNetworkGroup &configured_net_group,
    const inference_runner_params &params)
{
    std::map<std::string, std::vector<InputVStream>> res;
    TRY(const auto network_infos, configured_net_group.get_network_infos());
    for (const auto &network_info : network_infos) {
        auto quantized = (params.transform.format_type != HAILO_FORMAT_TYPE_FLOAT32);
        TRY(auto input_vstreams_params, configured_net_group.make_input_vstream_params(quantized,
            params.transform.format_type, HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_info.name));

        for (auto &vstream_params : input_vstreams_params) {
            vstream_params.second.pipeline_elements_stats_flags = inference_runner_params_to_pipeline_elem_stats_flags(params.pipeline_stats);
            vstream_params.second.vstream_stats_flags = inference_runner_params_to_vstream_stats_flags(params.pipeline_stats);
        }
        TRY(auto input_vstreams, VStreamsBuilder::create_input_vstreams(configured_net_group, input_vstreams_params));
        res.emplace(network_info.name, std::move(input_vstreams));
    }
    return res;
}

Expected<std::map<std::string, std::vector<OutputVStream>>> create_output_vstreams(ConfiguredNetworkGroup &configured_net_group,
    const inference_runner_params &params)
{
    std::map<std::string, std::vector<OutputVStream>> res;
    TRY(const auto network_infos, configured_net_group.get_network_infos());
    for (const auto &network_info : network_infos) {
        // Data is not quantized if format_type is explicitly float32, or if an output is NMS (which also enforces float32 output)
        // We don't cover a case of multiple outputs where only some of them are NMS (no such model currently), and anyway it is handled in run2
        TRY(const auto vstream_infos, configured_net_group.get_output_vstream_infos());
        auto nms_output = std::any_of(vstream_infos.begin(), vstream_infos.end(), [] (const hailo_vstream_info_t &output_info) {
            return HailoRTCommon::is_nms(output_info);
        });
        auto quantized = ((params.transform.format_type != HAILO_FORMAT_TYPE_FLOAT32) && !nms_output);
        TRY(auto output_vstreams_params, configured_net_group.make_output_vstream_params(quantized,
            params.transform.format_type, HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_info.name));

        for (auto &vstream_params : output_vstreams_params) {
            vstream_params.second.pipeline_elements_stats_flags = inference_runner_params_to_pipeline_elem_stats_flags(params.pipeline_stats);
            vstream_params.second.vstream_stats_flags = inference_runner_params_to_vstream_stats_flags(params.pipeline_stats);
        }
        TRY(auto output_vstreams, VStreamsBuilder::create_output_vstreams(configured_net_group, output_vstreams_params));
        res.emplace(network_info.name, std::move(output_vstreams));
    }
    return res;
}

Expected<std::map<std::string, std::vector<std::reference_wrapper<InputStream>>>> create_input_streams(ConfiguredNetworkGroup &configured_net_group)
{
    std::map<std::string, std::vector<std::reference_wrapper<InputStream>>> res;
    TRY(const auto network_infos, configured_net_group.get_network_infos());
    for (const auto &network_info : network_infos) {
        TRY(auto input_streams, configured_net_group.get_input_streams_by_network(network_info.name));
        res.emplace(network_info.name, std::move(input_streams));
    }
    return res;
}

Expected<std::map<std::string, std::vector<std::reference_wrapper<OutputStream>>>> create_output_streams(ConfiguredNetworkGroup &configured_net_group)
{
    std::map<std::string, std::vector<std::reference_wrapper<OutputStream>>> res;
    TRY(const auto network_infos, configured_net_group.get_network_infos());
    for (const auto &network_info : network_infos) {
        TRY(auto output_streams, configured_net_group.get_output_streams_by_network(network_info.name));
        res.emplace(network_info.name, std::move(output_streams));
    }
    return res;
}

// TODO: HRT-5177 create output buffers inside run_streaming
template< typename RecvObject>
Expected<std::map<std::string, BufferPtr>> create_output_buffers(
    std::map<std::string, std::vector<std::reference_wrapper<RecvObject>>> &recv_objects_per_network)
{
    std::map<std::string, BufferPtr> dst_data;
    for (auto &recv_objects : recv_objects_per_network) {
        for (auto &recv_object : recv_objects.second) {
            TRY(dst_data[recv_object.get().name()],
                Buffer::create_shared(recv_object.get().get_frame_size()));
        }
    }

    return dst_data;
}

std::pair<std::string, uint16_t> get_network_to_batch(const std::string &name_to_batch)
{
    /* name_to_batch is formed like <network_name>=<batch_size>
       We know the string is valid - we check it in NetworkBatchValidator on inference params */
    size_t first_delimiter = name_to_batch.find("=");
    auto batch_size_str = name_to_batch.substr(first_delimiter + 1);
    auto network_name = name_to_batch.substr(0, first_delimiter);
    return std::make_pair(network_name, static_cast<uint16_t>(std::stoi(batch_size_str)));
}

uint16_t get_batch_size(const inference_runner_params &params, const std::string &network_name)
{
    uint16_t batch_size = params.batch_size;

    /* params.batch_per_network is a partial list of networks.
       If a network is not in it, it gets the network_group_batch (params.batch_size) */
    for (auto &name_to_batch_str : params.batch_per_network) {
        auto name_to_batch = get_network_to_batch(name_to_batch_str);
        if (network_name == name_to_batch.first) {
            batch_size = name_to_batch.second;
            break;
        }
    }
    return (HAILO_DEFAULT_BATCH_SIZE == batch_size ? 1 : batch_size);
}

Expected<std::map<std::string, ConfigureNetworkParams>> get_configure_params(const inference_runner_params &params, hailort::Hef &hef, hailo_stream_interface_t interface)
{
    std::map<std::string, ConfigureNetworkParams> configure_params = {};

    hailo_configure_params_t config_params = {};
    hailo_status status = hailo_init_configure_params(reinterpret_cast<hailo_hef>(&hef), interface, &config_params);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* params.batch_per_network is a partial list of networks.
       If a network is not in it, it gets the network_group_batch (params.batch_size) */
    if (params.batch_per_network.empty()) {
        for (size_t network_group_idx = 0; network_group_idx < config_params.network_group_params_count; network_group_idx++) {
            config_params.network_group_params[network_group_idx].batch_size = params.batch_size;
        }
    } else {
        for (auto &name_to_batch_str : params.batch_per_network) {
            auto name_to_batch = get_network_to_batch(name_to_batch_str);
            auto network_name = name_to_batch.first;
            auto batch_size = name_to_batch.second;
            for (size_t network_group_idx = 0; network_group_idx < config_params.network_group_params_count; network_group_idx++) {
                bool found = false;
                for (uint8_t network_idx = 0; network_idx < config_params.network_group_params[network_group_idx].network_params_by_name_count; network_idx++) {
                    if (0 == strcmp(network_name.c_str(), config_params.network_group_params[network_group_idx].network_params_by_name[network_idx].name)) {
                        config_params.network_group_params[network_group_idx].network_params_by_name[network_idx].network_params.batch_size = batch_size;
                        found = true;
                    }
                }
                CHECK_AS_EXPECTED(found, HAILO_INVALID_ARGUMENT, "Did not find any network named {}. Use 'parse-hef' option to see network names.",
                    network_name);
            }
        }
    }
    for (size_t network_group_idx = 0; network_group_idx < config_params.network_group_params_count; network_group_idx++) {
        config_params.network_group_params[network_group_idx].power_mode = params.power_mode;
        configure_params.emplace(std::string(config_params.network_group_params[network_group_idx].name),
            ConfigureNetworkParams(config_params.network_group_params[network_group_idx]));

        if (params.measure_latency) {
            configure_params[std::string(config_params.network_group_params[network_group_idx].name)].latency |= HAILO_LATENCY_MEASURE;
        }
    }

    return configure_params;
}

template<typename SendObject, typename RecvObject>
static hailo_status run_streaming_impl(std::shared_ptr<ConfiguredNetworkGroup> configured_net_group,
    const std::map<std::string, BufferPtr> &input_dataset,
    std::map<std::string, BufferPtr> &output_buffers,
    const inference_runner_params &params,
    std::vector<std::reference_wrapper<SendObject>> &send_objects,
    std::vector<std::reference_wrapper<RecvObject>> &recv_objects,
    const std::string &network_name,
    InferProgress &progress_bar,
    NetworkInferResult &inference_result)
{
    // latency resources init
    if (params.measure_overall_latency) {
        CHECK((send_objects.size() == 1), HAILO_INVALID_OPERATION, "Overall latency measurement not support multiple inputs network");
    }
    std::set<std::string> output_names;
    for (auto &output : recv_objects) {
        output_names.insert(output.get().name());
    }

    LatencyMeter overall_latency_meter(output_names, OVERALL_LATENCY_TIMESTAMPS_LIST_LENGTH);
    Barrier barrier(send_objects.size() + recv_objects.size());

    std::vector<std::atomic_size_t> frames_recieved_per_output(recv_objects.size());
    for (auto &count : frames_recieved_per_output) {
        count = 0;
    }
    auto batch_size = get_batch_size(params, network_name);

    std::vector<AsyncThreadPtr<hailo_status>> results;

    TRY(auto network_progress_bar,
        progress_bar.create_network_progress_bar(configured_net_group, network_name));
    const auto start = std::chrono::high_resolution_clock::now();

    // Launch async read/writes
    uint32_t output_index = 0;
    auto first = true;
    for (auto& recv_object : recv_objects) {
        auto &frames_recieved = frames_recieved_per_output[output_index];
        results.emplace_back(std::make_unique<AsyncThread<hailo_status>>("RECV",
            [network_progress_bar, params, &recv_object, &output_buffers, first, &barrier, &overall_latency_meter,
            &frames_recieved, batch_size]() {
                auto res = recv_loop(params, recv_object.get(), network_progress_bar, barrier, overall_latency_meter,
                    output_buffers, frames_recieved, first, batch_size);
                if (HAILO_SUCCESS != res) {
                    barrier.terminate();
                }
                return res;
            }
        ));
        first = false;
        ++output_index;
    }
    for (auto &send_object : send_objects) {
        results.emplace_back(std::make_unique<AsyncThread<hailo_status>>("SEND",
            [params, &send_object, &input_dataset, &barrier, &overall_latency_meter, batch_size]() -> hailo_status {
                auto res = send_loop(params, send_object.get(), input_dataset, barrier, overall_latency_meter, batch_size);
                if (HAILO_SUCCESS != res) {
                    barrier.terminate();
                }
                return res;
            }
        ));
    }

    if (0 < params.time_to_run) {
        auto status = wait_for_exit_with_timeout(std::chrono::seconds(params.time_to_run));
        CHECK_SUCCESS(status);

        status = configured_net_group->shutdown();
        barrier.terminate();
        CHECK_SUCCESS(status);
    }

    // Wait for all results
    auto error_status = HAILO_SUCCESS;
    for (auto& result : results) {
        auto status = result->get();
        if (HAILO_STREAM_ABORT == status) {
            continue;
        }
        if (HAILO_SUCCESS != status) {
            error_status = status;
            LOGGER__ERROR("Failed waiting for threads with status {}", error_status);
        }
    }
    CHECK_SUCCESS(error_status);

    auto end = std::chrono::high_resolution_clock::now();

    // Update inference_result struct
    size_t min_frame_count_recieved = *std::min_element(frames_recieved_per_output.begin(),
        frames_recieved_per_output.end());

    inference_result.m_frames_count = min_frame_count_recieved;

    // TODO: HRT-7798
    if (!params.vdevice_params.multi_process_service) {
        TRY(auto network_input_streams, configured_net_group->get_input_streams_by_network(network_name));
        inference_result.m_total_send_frame_size = total_send_frame_size(network_input_streams);
        TRY(auto network_output_streams, configured_net_group->get_output_streams_by_network(network_name));
        inference_result.m_total_recv_frame_size = total_recv_frame_size(network_output_streams);
    }

    if (params.measure_latency) {
        if (auto hw_latency = configured_net_group->get_latency_measurement(network_name)) {
            auto hw_latency_p = make_shared_nothrow<std::chrono::nanoseconds>(hw_latency->avg_hw_latency);
            CHECK_NOT_NULL(hw_latency_p, HAILO_OUT_OF_HOST_MEMORY);
            inference_result.m_hw_latency = std::move(hw_latency_p);
        }
    } else {
        inference_result.m_infer_duration = std::make_unique<double>(std::chrono::duration<double>(end - start).count());
    }

    if (params.measure_overall_latency) {
        TRY(auto overall_latency, overall_latency_meter.get_latency(true));
        inference_result.m_overall_latency = std::make_unique<std::chrono::nanoseconds>(std::move(overall_latency));
    }

    return HAILO_SUCCESS;
}

template<typename SendObject, typename RecvObject>
static Expected<InferResult> run_streaming(const std::vector<std::shared_ptr<ConfiguredNetworkGroup>> &configured_net_groups,
    const std::vector<std::map<std::string, BufferPtr>> &input_datasets,
    std::vector<std::map<std::string, BufferPtr>> &output_buffers,
    const inference_runner_params &params,
    std::vector<std::map<std::string, std::vector<std::reference_wrapper<SendObject>>>> &send_objects_per_network_group,
    std::vector<std::map<std::string, std::vector<std::reference_wrapper<RecvObject>>>> &recv_objects_per_network_group)
{
    CHECK_AS_EXPECTED(send_objects_per_network_group.size() == recv_objects_per_network_group.size(), HAILO_INTERNAL_FAILURE,
        "Not all network groups parsed correctly.");
    CHECK_AS_EXPECTED(configured_net_groups.size() == recv_objects_per_network_group.size(), HAILO_INTERNAL_FAILURE,
        "Not all network groups parsed correctly. configured_net_groups.size(): {}, recv_objects_per_network_group: {}", configured_net_groups.size(), recv_objects_per_network_group.size());

    std::vector<NetworkGroupInferResult> results_per_network_group;

    std::vector<std::vector<AsyncThreadPtr<hailo_status>>> networks_threads_status; // Vector of threads for each network group
    networks_threads_status.reserve(configured_net_groups.size());
    std::vector<std::map<std::string, NetworkInferResult>> networks_results; // Map of networks results for each network group
    networks_results.reserve(configured_net_groups.size());

    TRY(auto progress_bar, InferProgress::create(params, std::chrono::seconds(1)));
    for (size_t network_group_index = 0; network_group_index < configured_net_groups.size(); network_group_index++) {
        networks_threads_status.emplace_back();
        networks_results.emplace_back();
        CHECK_AS_EXPECTED(send_objects_per_network_group[network_group_index].size() == recv_objects_per_network_group[network_group_index].size(), HAILO_INTERNAL_FAILURE,
            "Not all networks parsed correctly in network group {}.", configured_net_groups[network_group_index]->name());

        // TODO: support AsyncThreadPtr for Expected, and use it instead of status
        networks_threads_status[network_group_index].reserve(send_objects_per_network_group[network_group_index].size());

        // TODO (HRT-5789): instead of init this map and giving it to run_streaming_impl, change AsyncThread to return Expected
        for (auto &network_name_pair : send_objects_per_network_group[network_group_index]) {
            networks_results[network_group_index].emplace(network_name_pair.first, NetworkInferResult());
        }
    }

    if (params.show_progress) {
        progress_bar->start();
    }

    for (size_t network_group_index = 0; network_group_index < configured_net_groups.size(); network_group_index++) {
        for (auto &network_name_pair : send_objects_per_network_group[network_group_index]) {
            CHECK_AS_EXPECTED(contains(recv_objects_per_network_group[network_group_index], network_name_pair.first), HAILO_INTERNAL_FAILURE,
                "Not all networks was parsed correctly.");
            auto network_name = network_name_pair.first;
            networks_threads_status[network_group_index].emplace_back(std::make_unique<AsyncThread<hailo_status>>(fmt::format("NG_INFER {}", network_group_index),
                [network_group_index, &configured_net_groups, &input_datasets, &output_buffers, &params, &send_objects_per_network_group,
                    &recv_objects_per_network_group, network_name, &progress_bar, &networks_results]() {
                    return run_streaming_impl(configured_net_groups[network_group_index], input_datasets[network_group_index],
                        output_buffers[network_group_index], params,
                        send_objects_per_network_group[network_group_index].at(network_name),
                        recv_objects_per_network_group[network_group_index].at(network_name),
                        network_name, *progress_bar, networks_results[network_group_index].at(network_name));
                }
            ));
        }
    }

    for (size_t network_group_index = 0; network_group_index < configured_net_groups.size(); network_group_index++) {
        // Wait for all results
        for (auto& status : networks_threads_status[network_group_index]) {
            auto network_status = status->get();
            CHECK_SUCCESS_AS_EXPECTED(network_status);
        }
    }

    if (params.show_progress) {
        progress_bar->finish();
    }

    for (size_t network_group_index = 0; network_group_index < configured_net_groups.size(); network_group_index++) {
        NetworkGroupInferResult network_group_result(configured_net_groups[network_group_index]->name(),
            std::move(networks_results[network_group_index]));

        if (should_measure_pipeline_stats(params)) {
            network_group_result.update_pipeline_stats(send_objects_per_network_group[network_group_index],
                recv_objects_per_network_group[network_group_index]);
        }
        results_per_network_group.emplace_back(std::move(network_group_result));
    }

    // Update final_result struct - with all inferences results
    InferResult final_result(std::move(results_per_network_group));
    return final_result;
}

static Expected<InferResult> run_inference(const std::vector<std::shared_ptr<ConfiguredNetworkGroup>> &configured_net_groups,
    const std::vector<std::map<std::string, BufferPtr>> &input_datasets,
    const inference_runner_params &params)
{
    switch (params.mode) {
    case InferMode::STREAMING:
    {
        std::vector<std::shared_ptr<std::map<std::string, std::vector<InputVStream>>>> input_vstreams;
        input_vstreams.reserve(configured_net_groups.size());
        std::vector<std::shared_ptr<std::map<std::string, std::vector<OutputVStream>>>> output_vstreams;
        output_vstreams.reserve(configured_net_groups.size());

        std::vector<std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>>> input_vstreams_refs(configured_net_groups.size(),
            std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>>());
        std::vector<std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>>> output_vstreams_refs(configured_net_groups.size(),
            std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>>());

        std::vector<std::map<std::string, hailort::BufferPtr>> output_buffers(configured_net_groups.size(),
            std::map<std::string, hailort::BufferPtr>());

        for (size_t network_group_index = 0; network_group_index < configured_net_groups.size(); network_group_index++) {
            input_vstreams.emplace_back();
            output_vstreams.emplace_back();
            TRY(auto in_vstreams, create_input_vstreams(*configured_net_groups[network_group_index], params));
            auto in_vstreams_ptr = make_shared_nothrow<std::map<std::string, std::vector<InputVStream>>>(std::move(in_vstreams));
            CHECK_NOT_NULL_AS_EXPECTED(in_vstreams_ptr, HAILO_OUT_OF_HOST_MEMORY);
            input_vstreams[network_group_index] = in_vstreams_ptr;

            TRY(auto out_vstreams, create_output_vstreams(*configured_net_groups[network_group_index], params));
            auto out_vstreams_ptr = make_shared_nothrow<std::map<std::string, std::vector<OutputVStream>>>(std::move(out_vstreams));
            CHECK_NOT_NULL_AS_EXPECTED(out_vstreams_ptr, HAILO_OUT_OF_HOST_MEMORY);
            output_vstreams[network_group_index] = out_vstreams_ptr;

            // run_streaming function should get reference_wrappers to vstreams instead of the instances themselves
            for (auto &input_vstreams_per_network : *input_vstreams[network_group_index]) {
                std::vector<std::reference_wrapper<InputVStream>> input_refs;
                for (auto &input_vstream : input_vstreams_per_network.second) {
                    input_refs.emplace_back(input_vstream);
                }
                input_vstreams_refs[network_group_index].emplace(input_vstreams_per_network.first, input_refs);
            }
            for (auto &output_vstreams_per_network : *output_vstreams[network_group_index]) {
                std::vector<std::reference_wrapper<OutputVStream>> output_refs;
                for (auto &output_vstream : output_vstreams_per_network.second) {
                    output_refs.emplace_back(output_vstream);
                }
                output_vstreams_refs[network_group_index].emplace(output_vstreams_per_network.first, output_refs);
            }

            TRY(output_buffers[network_group_index], create_output_buffers(output_vstreams_refs[network_group_index]));
        }

        auto res = run_streaming<InputVStream, OutputVStream>(configured_net_groups, input_datasets,
            output_buffers, params, input_vstreams_refs, output_vstreams_refs);

        if (!params.dot_output.empty()) {
            const auto status = GraphPrinter::write_dot_file(input_vstreams_refs, output_vstreams_refs, params.hef_path,
                params.dot_output, should_measure_pipeline_stats(params));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }

        input_vstreams.clear();
        output_vstreams.clear();

        CHECK_EXPECTED(res); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
        return res;
    }
    case InferMode::HW_ONLY:
    {
        std::vector<std::map<std::string, std::vector<std::reference_wrapper<InputStream>>>> input_streams_refs(configured_net_groups.size(),
            std::map<std::string, std::vector<std::reference_wrapper<InputStream>>>());
        std::vector<std::map<std::string, std::vector<std::reference_wrapper<OutputStream>>>> output_streams_refs(configured_net_groups.size(),
            std::map<std::string, std::vector<std::reference_wrapper<OutputStream>>>());

        std::vector<std::map<std::string, hailort::BufferPtr>> output_buffers(configured_net_groups.size(),
            std::map<std::string, hailort::BufferPtr>());

        for (size_t network_group_index = 0; network_group_index < configured_net_groups.size(); network_group_index++) {
            TRY(input_streams_refs[network_group_index], create_input_streams(*configured_net_groups[network_group_index]));
            TRY(output_streams_refs[network_group_index], create_output_streams(*configured_net_groups[network_group_index]));
            TRY(output_buffers[network_group_index], create_output_buffers(output_streams_refs[network_group_index]));
        }

        return run_streaming<InputStream, OutputStream>(configured_net_groups, input_datasets, output_buffers,
            params, input_streams_refs, output_streams_refs);
    }
    default:
        return make_unexpected(HAILO_INVALID_OPERATION);
    }
}

static Expected<std::map<std::string, BufferPtr>> create_uniformed_dataset(
    const std::pair<std::vector<hailo_stream_info_t>, std::vector<hailo_vstream_info_t>> &input_infos, const hailo_transform_params_t &trans_params,
    InferMode mode)
{
    std::map<std::string, BufferPtr> dataset;

    if (InferMode::HW_ONLY == mode) {
        for (const auto &input_stream_info : input_infos.first) {
            const auto frame_size = input_stream_info.hw_frame_size;
            auto buffer = create_uniformed_buffer_shared(frame_size);
            if (!buffer) {
                std::cerr << "Out of memory, tried to allocate " << frame_size << std::endl;
                return make_unexpected(buffer.status());
            }
            dataset.emplace(std::string(input_stream_info.name), buffer.release());
        }
    } else {
        for (const auto &input_vstream_info : input_infos.second) {
            const auto frame_size = HailoRTCommon::get_frame_size(input_vstream_info, trans_params.user_buffer_format);
            auto buffer = create_uniformed_buffer_shared(frame_size);
            if (!buffer) {
                std::cerr << "Out of memory, tried to allocate " << frame_size << std::endl;
                return make_unexpected(buffer.status());
            }
            dataset.emplace(std::string(input_vstream_info.name), buffer.release());
        }
    }

    return dataset;
}

static Expected<std::map<std::string, BufferPtr>> create_dataset_from_files(
    const std::pair<std::vector<hailo_stream_info_t>, std::vector<hailo_vstream_info_t>> &input_infos, const std::vector<std::string> &input_files,
    const hailo_transform_params_t &trans_params, InferMode mode)
{
    // When creating dataset from files we always care about the logic-inputs (e.g. vstreams)
    CHECK_AS_EXPECTED(input_infos.second.size() == input_files.size(),
        HAILO_INVALID_ARGUMENT, "Number of input files ({}) must be equal to the number of inputs ({})", input_files.size(), input_infos.second.size());

    std::map<std::string, std::string> file_paths;
    if ((input_infos.second.size() == 1) && (input_files[0].find("=") == std::string::npos)) { // Legacy single input format
        file_paths.emplace(std::string(input_infos.second.begin()->name), input_files[0]);
    }
    else {
        file_paths = format_strings_to_key_value_pairs(input_files);
    }

    std::map<std::string, BufferPtr> dataset;
    for (const auto &input_vstream_info : input_infos.second) {
        const auto host_frame_size = HailoRTCommon::get_frame_size(input_vstream_info, trans_params.user_buffer_format);
        const auto stream_name = std::string(input_vstream_info.name);
        CHECK_AS_EXPECTED(stream_name.find("=") == std::string::npos,
            HAILO_INVALID_ARGUMENT, "stream inputs must not contain '=' characters: {}", stream_name);

        const auto file_path_it = file_paths.find(stream_name);
        CHECK_AS_EXPECTED(file_paths.end() != file_path_it, HAILO_INVALID_ARGUMENT, "Missing input file for input: {}", stream_name);
        
        TRY(auto host_buffer, read_binary_file(file_path_it->second));
        CHECK_AS_EXPECTED((host_buffer.size() % host_frame_size) == 0, HAILO_INVALID_ARGUMENT,
            "Input file ({}) size {} must be a multiple of the frame size {} ({})", file_path_it->second, host_buffer.size(), host_frame_size, stream_name);

        if (InferMode::HW_ONLY == mode) {
            auto matching_stream_info = std::find_if(input_infos.first.begin(), input_infos.first.end(), [&stream_name] (const auto &stream_info) {
                return std::string(stream_info.name) == stream_name;
            });
            CHECK_AS_EXPECTED(matching_stream_info != input_infos.first.end(), HAILO_INVALID_OPERATION, "Failed to find raw-stream with name {}.", stream_name);
            const size_t frames_count = (host_buffer.size() / host_frame_size);
            const size_t hw_frame_size = matching_stream_info->hw_frame_size;
            const size_t hw_buffer_size = frames_count * hw_frame_size;
            TRY(auto hw_buffer, Buffer::create_shared(hw_buffer_size));
            TRY(auto transform_context, InputTransformContext::create(*matching_stream_info, trans_params));
            
            for (size_t i = 0; i < frames_count; i++) {
                MemoryView host_data(static_cast<uint8_t*>(host_buffer.data() + (i*host_frame_size)), host_frame_size);
                MemoryView hw_data(static_cast<uint8_t*>(hw_buffer->data() + (i*hw_frame_size)), hw_frame_size);

                auto status = transform_context->transform(host_data, hw_data);
                CHECK_SUCCESS_AS_EXPECTED(status);
            }
            dataset[stream_name] = std::move(hw_buffer);
        } else {
            auto host_buffer_shared = make_shared_nothrow<Buffer>(std::move(host_buffer));
            CHECK_NOT_NULL_AS_EXPECTED(host_buffer_shared, HAILO_OUT_OF_HOST_MEMORY);
            dataset[stream_name] = host_buffer_shared;
        }
    }

    return dataset;
}

static Expected<std::vector<std::map<std::string, BufferPtr>>> create_dataset(
    const std::vector<std::shared_ptr<ConfiguredNetworkGroup>> &network_groups,
    const inference_runner_params &params)
{
    std::vector<std::map<std::string, BufferPtr>> results;
    results.reserve(network_groups.size());
    hailo_transform_params_t trans_params = {};
    trans_params.transform_mode = (params.transform.transform ? HAILO_STREAM_TRANSFORM_COPY : HAILO_STREAM_NO_TRANSFORM);
    trans_params.user_buffer_format.order = HAILO_FORMAT_ORDER_AUTO;
    trans_params.user_buffer_format.flags = (params.transform.quantized ? HAILO_FORMAT_FLAGS_QUANTIZED : HAILO_FORMAT_FLAGS_NONE);
    trans_params.user_buffer_format.type = params.transform.format_type;

    // Vector of len(ng.conut), each element is pair of all input_stream_infos, and all input_vstream_infos
    std::vector<std::pair<std::vector<hailo_stream_info_t>, std::vector<hailo_vstream_info_t>>> input_infos;
    for (auto &network_group : network_groups) {
        TRY(const auto all_streams_infos, network_group->get_all_stream_infos());
        std::vector<hailo_stream_info_t> group_input_stream_infos;
        std::copy_if(all_streams_infos.begin(), all_streams_infos.end(), std::back_inserter(group_input_stream_infos), [](const auto &info) {
            return info.direction == HAILO_H2D_STREAM;
        });
        TRY(const auto input_vstreams_infos, network_group->get_input_vstream_infos());
        input_infos.push_back({group_input_stream_infos, std::move(input_vstreams_infos)});
    }

    if (!params.inputs_name_and_file_path.empty()) {
        for (auto &group_input_infos : input_infos) {
            TRY(auto network_group_dataset, create_dataset_from_files(group_input_infos, params.inputs_name_and_file_path,
                trans_params, params.mode));
            results.emplace_back(std::move(network_group_dataset));
        }
    } else {
        for (auto &group_input_infos : input_infos) {
            TRY(auto network_group_dataset, create_uniformed_dataset(group_input_infos, trans_params, params.mode));
            results.emplace_back(std::move(network_group_dataset));
        }
    }
    return results;
}

Expected<InferResult> activate_and_run_single_device(
    Device &device,
    const std::vector<std::shared_ptr<ConfiguredNetworkGroup>> &network_groups,
    const inference_runner_params &params)
{
    CHECK_AS_EXPECTED(1 == network_groups.size(), HAILO_INVALID_OPERATION, "Inference is not supported on HEFs with multiple network groups");
    TRY(auto activated_net_group, network_groups[0]->activate(), "Failed activate network_group");
    TRY(auto input_dataset, create_dataset(network_groups, params));
    TRY(auto caps, device.get_capabilities());

    hailo_power_measurement_types_t measurement_type = HAILO_POWER_MEASUREMENT_TYPES__MAX_ENUM;
    bool should_measure_power = false;
    if ((ShouldMeasurePower::YES == params.power_measurement.measure_power) ||
        ((ShouldMeasurePower::AUTO_DETECT == params.power_measurement.measure_power) && caps.power_measurements)) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__POWER;
        should_measure_power = true;
    } else if (params.power_measurement.measure_current) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__CURRENT;
        should_measure_power = true;
    }

    std::shared_ptr<LongPowerMeasurement> long_power_measurement_ptr = nullptr;
    if (should_measure_power) {
        TRY(auto long_power_measurement, PowerMeasurementSubcommand::start_power_measurement(device,
            HAILO_DVM_OPTIONS_AUTO, measurement_type, params.power_measurement.sampling_period, params.power_measurement.averaging_factor));
        long_power_measurement_ptr = make_shared_nothrow<LongPowerMeasurement>(std::move(long_power_measurement));
        CHECK_NOT_NULL_AS_EXPECTED(long_power_measurement_ptr, HAILO_OUT_OF_HOST_MEMORY);
    }

    bool should_measure_temp = params.measure_temp;
    TRY(auto temp_measure, TemperatureMeasurement::create_shared(device));
    if (should_measure_temp) {
        auto status = temp_measure->start_measurement();
        CHECK_SUCCESS_AS_EXPECTED(status, "Failed to get chip's temperature");
    }

    TRY(auto inference_result, run_inference(network_groups, input_dataset, params), "Error while running inference");

    std::vector<std::reference_wrapper<Device>> device_refs;
    device_refs.push_back(device);
    inference_result.initialize_measurements(device_refs);

    if (should_measure_power) {
        auto status = long_power_measurement_ptr->stop();
        CHECK_SUCCESS_AS_EXPECTED(status);

        if (params.power_measurement.measure_current) {
            status = inference_result.set_current_measurement(device.get_dev_id(), std::move(long_power_measurement_ptr));
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            status = inference_result.set_power_measurement(device.get_dev_id(), std::move(long_power_measurement_ptr));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
        inference_result.power_measurements_are_valid = true;
    }

    if (should_measure_temp) {
        temp_measure->stop_measurement();
        auto temp_measure_p = make_shared_nothrow<AccumulatorResults>(temp_measure->get_data());
        CHECK_NOT_NULL_AS_EXPECTED(temp_measure_p, HAILO_OUT_OF_HOST_MEMORY);
        auto status = inference_result.set_temp_measurement(device.get_dev_id(), std::move(temp_measure_p));
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    return inference_result;
}

Expected<size_t> get_min_inferred_frames_count(InferResult &inference_result)
{
    size_t min_frames_count = UINT32_MAX;
    for (auto &network_group_results : inference_result.network_group_results()) {
        for (const auto &network_results_pair : network_group_results.results_per_network()) {
            TRY(const auto frames_count, network_group_results.frames_count(network_results_pair.first));
            min_frames_count = std::min(frames_count, min_frames_count);
        }
    }
    return min_frames_count;
}

Expected<InferResult> run_command_hef_single_device(const inference_runner_params &params)
{
    TRY(auto devices, create_devices(params.vdevice_params.device_params), "Failed creating device");
    /* This function supports controls for multiple devices.
       We validate there is only 1 device generated as we are on a single device flow */
    CHECK_AS_EXPECTED(1 == devices.size(), HAILO_INTERNAL_FAILURE);
    auto &device = devices[0];

    TRY(auto hef, Hef::create(params.hef_path.c_str()), "Failed reading hef file {}", params.hef_path);
    TRY(const auto interface, device->get_default_streams_interface(), "Failed to get default streams interface");
    TRY(auto configure_params, get_configure_params(params, hef, interface));
    TRY(auto network_group_list, device->configure(hef, configure_params), "Failed configure device from hef");

    if (use_batch_to_measure_opt(params)) {
        auto status = DownloadActionListCommand::set_batch_to_measure(*device, params.runtime_data.batch_to_measure);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto inference_result = activate_and_run_single_device(*device, network_group_list, params);

    if (use_batch_to_measure_opt(params) && (0 == params.frames_count) && inference_result) {
        TRY(auto min_frames_count, get_min_inferred_frames_count(inference_result.value()));
        if (min_frames_count < params.runtime_data.batch_to_measure) {
            LOGGER__WARNING("Number of frames sent ({}) is smaller than --batch-to-measure ({}), "
                "hence timestamps will not be updated in runtime data", min_frames_count,
                params.runtime_data.batch_to_measure);
        }
    }

    if (params.runtime_data.collect_runtime_data) {
        const auto runtime_data_output_path = format_runtime_data_output_path(
            params.runtime_data.runtime_data_output_path, params.hef_path);
        DownloadActionListCommand::execute(*device, runtime_data_output_path, network_group_list,
            params.hef_path);
    }

    CHECK_EXPECTED(inference_result); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
    return inference_result;
}


Expected<InferResult> activate_and_run_vdevice(
    std::vector<std::reference_wrapper<Device>> &physical_devices,
    bool scheduler_is_used,
    const std::vector<std::shared_ptr<ConfiguredNetworkGroup>> &network_groups,
    const inference_runner_params &params)
{
    std::unique_ptr<ActivatedNetworkGroup> activated_network_group;
    if (!scheduler_is_used) {
        TRY(activated_network_group, network_groups[0]->activate());
    }

    TRY(const auto input_dataset, create_dataset(network_groups, params), "Failed creating input dataset");
    // we currently support all devices or none for power measurements
    auto all_phy_devices_support_power_measurements = true;
    for (const auto &device : physical_devices) {
        TRY(const auto caps, device.get().get_capabilities(), "Failed getting device capabilities");
        if (!caps.power_measurements) {
            all_phy_devices_support_power_measurements = false;
            break;
        }
    }

    hailo_power_measurement_types_t measurement_type = HAILO_POWER_MEASUREMENT_TYPES__MAX_ENUM;
    bool should_measure_power = false;
    if ((ShouldMeasurePower::YES == params.power_measurement.measure_power) ||
        ((ShouldMeasurePower::AUTO_DETECT == params.power_measurement.measure_power) && all_phy_devices_support_power_measurements)) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__POWER;
        should_measure_power = true;
    } else if (params.power_measurement.measure_current) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__CURRENT;
        should_measure_power = true;
    }

    std::map<std::string, std::shared_ptr<LongPowerMeasurement>> power_measurements;
    if (should_measure_power) {
        for (auto &device : physical_devices) {
            TRY(auto long_power_measurement, PowerMeasurementSubcommand::start_power_measurement(device,
                    HAILO_DVM_OPTIONS_AUTO, measurement_type, params.power_measurement.sampling_period, params.power_measurement.averaging_factor),
                "Failed starting power measurement on device {}", device.get().get_dev_id());
            auto long_power_measurement_p = make_shared_nothrow<LongPowerMeasurement>(std::move(long_power_measurement));
            CHECK_NOT_NULL_AS_EXPECTED(long_power_measurement_p, HAILO_OUT_OF_HOST_MEMORY);
            power_measurements.emplace(device.get().get_dev_id(), std::move(long_power_measurement_p));
        }
    }

    std::map<std::string, std::shared_ptr<TemperatureMeasurement>> temp_measurements;
    if (params.measure_temp) {
        for (auto &device : physical_devices) {
            TRY(auto temp_measure, TemperatureMeasurement::create_shared(device));
            auto status = temp_measure->start_measurement();
            CHECK_SUCCESS_AS_EXPECTED(status, "Failed starting temperature measurement on device {}", device.get().get_dev_id());
            temp_measurements.emplace(device.get().get_dev_id(), temp_measure);
        }
    }

    TRY(auto inference_result, run_inference(network_groups, input_dataset, params), "Error failed running inference");
    inference_result.initialize_measurements(physical_devices);

    if (should_measure_power) {
        auto status = HAILO_SUCCESS;
        for (auto &power_measure_pair : power_measurements) {
            auto measurement_status = power_measure_pair.second->stop();
            if (HAILO_SUCCESS != measurement_status) {
                // The returned status will be the last non-success status.
                status = measurement_status;
                LOGGER__ERROR("Failed stopping power measurement on device {} with status {}", power_measure_pair.first, measurement_status);
            } else {
                auto set_measurement_status = HAILO_UNINITIALIZED;
                if (params.power_measurement.measure_current) {
                    set_measurement_status = inference_result.set_current_measurement(power_measure_pair.first, std::move(power_measure_pair.second));
                } else {
                    set_measurement_status = inference_result.set_power_measurement(power_measure_pair.first, std::move(power_measure_pair.second));
                }
                if (HAILO_SUCCESS != set_measurement_status) {
                    status = set_measurement_status;
                    LOGGER__ERROR("Failed setting power measurement to inference result with status {}", set_measurement_status);
                }
            }
        }
        CHECK_SUCCESS_AS_EXPECTED(status);
        inference_result.power_measurements_are_valid = true;
    }

    if (params.measure_temp) {
        for(const auto &temp_measure_pair : temp_measurements) {
            temp_measure_pair.second->stop_measurement();
            auto temp_measure_p = make_shared_nothrow<AccumulatorResults>(temp_measure_pair.second->get_data());
            CHECK_NOT_NULL_AS_EXPECTED(temp_measure_p, HAILO_OUT_OF_HOST_MEMORY);
            auto status = inference_result.set_temp_measurement(temp_measure_pair.first, std::move(temp_measure_p));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }

    return inference_result;
}

Expected<InferResult> run_command_hef_vdevice(const inference_runner_params &params)
{
    TRY(auto hef, Hef::create(params.hef_path.c_str()), "Failed reading hef file {}", params.hef_path);
    TRY(auto network_groups_infos, hef.get_network_groups_infos());
    bool scheduler_is_used = (1 < network_groups_infos.size()) || params.vdevice_params.multi_process_service;

    hailo_vdevice_params_t vdevice_params = {};
    auto status = hailo_init_vdevice_params(&vdevice_params);
    CHECK_SUCCESS_AS_EXPECTED(status);
    if (params.vdevice_params.device_count != HAILO_DEFAULT_DEVICE_COUNT) {
        vdevice_params.device_count = params.vdevice_params.device_count;
    }
    std::vector<hailo_device_id_t> dev_ids;
    if (!params.vdevice_params.device_params.device_ids.empty()) {
        TRY(auto dev_ids_strs, get_device_ids(params.vdevice_params.device_params));
        TRY(dev_ids, HailoRTCommon::to_device_ids_vector(dev_ids_strs));

        vdevice_params.device_ids = dev_ids.data();
        vdevice_params.device_count = static_cast<uint32_t>(dev_ids.size());
    }
    vdevice_params.scheduling_algorithm = (scheduler_is_used) ? HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN : HAILO_SCHEDULING_ALGORITHM_NONE;
    vdevice_params.group_id = params.vdevice_params.group_id.c_str();
    vdevice_params.multi_process_service = params.vdevice_params.multi_process_service;
    TRY(auto vdevice, VDevice::create(vdevice_params), "Failed creating vdevice");

    std::vector<std::reference_wrapper<Device>> physical_devices;
    if (!params.vdevice_params.multi_process_service) {
        TRY(physical_devices, vdevice->get_physical_devices());
    }

    TRY(const auto interface, vdevice->get_default_streams_interface(), "Failed to get default streams interface");
    TRY(auto configure_params, get_configure_params(params, hef, interface), "Failed getting configure params");
    TRY(auto network_group_list, vdevice->configure(hef, configure_params), "Failed configure vdevice from hef");

    for (auto &device : physical_devices) {
        TRY(auto caps, device.get().get_capabilities(), "Failed getting device capabilities");

        CHECK_AS_EXPECTED((caps.power_measurements || (params.power_measurement.measure_power != ShouldMeasurePower::YES)),
            HAILO_INVALID_OPERATION, "Power measurement not supported. Disable the power-measure option");
        CHECK_AS_EXPECTED((caps.current_measurements || !(params.power_measurement.measure_current)),
            HAILO_INVALID_OPERATION, "Current measurement not supported. Disable the current-measure option");
        CHECK_AS_EXPECTED((caps.temperature_measurements || !(params.measure_temp)),
            HAILO_INVALID_OPERATION, "Temperature measurement not supported. Disable the temp-measure option");

        if (use_batch_to_measure_opt(params)) {
            status = DownloadActionListCommand::set_batch_to_measure(device.get(), params.runtime_data.batch_to_measure);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }

    TRY(auto infer_result, activate_and_run_vdevice(physical_devices, scheduler_is_used, network_group_list, params),
        "Error while running inference");

    for (auto &device : physical_devices) {
        if (use_batch_to_measure_opt(params) && (0 == params.frames_count)) {
            TRY(const auto min_frames_count, get_min_inferred_frames_count(infer_result));
            if (min_frames_count < params.runtime_data.batch_to_measure) {
                LOGGER__WARNING("Number of frames sent ({}) is smaller than --batch-to-measure ({}), "
                    "hence timestamps will not be updated in runtime data", min_frames_count,
                    params.runtime_data.batch_to_measure);
            }
        }

        if (params.runtime_data.collect_runtime_data) {
            auto output_path = (1 == physical_devices.size()) ? params.runtime_data.runtime_data_output_path :
                (std::string(device.get().get_dev_id()) + "_" + params.runtime_data.runtime_data_output_path);
            const auto runtime_data_output_path = format_runtime_data_output_path(output_path, params.hef_path);
            DownloadActionListCommand::execute(device.get(), runtime_data_output_path, network_group_list,
                params.hef_path);
        }
    }

    return infer_result;
}

Expected<bool> use_vdevice(const hailo_vdevice_params &params)
{
    if (params.device_count > 1) {
        return true;
    }

    if (params.device_params.device_ids.empty()) {
        // By default, if no device id was given, we assume vdevice is allowed.
        return true;
    }

    TRY(const auto device_type, Device::get_device_type(params.device_params.device_ids[0]));

    return device_type != Device::Type::ETH;
}

Expected<InferResult> run_command_hef(const inference_runner_params &params)
{
    TRY(auto use_vdevice, use_vdevice(params.vdevice_params));

    if (use_vdevice) {
        return run_command_hef_vdevice(params);
    }
    else {
        return run_command_hef_single_device(params);
    }
}

static hailo_status run_command_hefs_dir(const inference_runner_params &params, InferStatsPrinter &printer)
{
    hailo_status overall_status = HAILO_SUCCESS;
    bool contains_hef = false; 
    std::string hef_dir = params.hef_path;
    inference_runner_params curr_params = params;

    TRY(const auto files, Filesystem::get_files_in_dir_flat(hef_dir));

    for (const auto &full_path : files) {
        if (Filesystem::has_suffix(full_path, ".hef")) {
            contains_hef = true;
            curr_params.hef_path = full_path;
            std::cout << std::string(80, '*') << std::endl << "Inferring " << full_path << ":"<< std::endl;
            TRY(auto hef, Hef::create(full_path));
            auto network_groups_names = hef.get_network_groups_names();
            auto infer_stats = run_command_hef(curr_params);
            printer.print(network_groups_names , infer_stats);

            if (!infer_stats) {
                overall_status = infer_stats.status();
            }
        }
    }

    if (!contains_hef){
        std::cerr << "No HEF files were found in the directory: " << hef_dir << std::endl;
        return HAILO_INVALID_ARGUMENT;
    }

    return overall_status;
}

hailo_status run_command(const inference_runner_params &params)
{
    TRY(auto printer, InferStatsPrinter::create(params), "Failed to initialize infer stats printer");
    if (!params.csv_output.empty()) {
        printer.print_csv_header();
    }

    TRY(const auto is_dir, Filesystem::is_directory(params.hef_path.c_str()), "Failed checking if path is directory");

    if (is_dir){
        return run_command_hefs_dir(params, printer);
    } else {
        auto infer_stats = run_command_hef(params);
        TRY(auto hef, Hef::create(params.hef_path.c_str()));
        auto network_groups_names = hef.get_network_groups_names();
        printer.print(network_groups_names, infer_stats);
        return infer_stats.status();
    }
}

RunCommand::RunCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("run", "Run a compiled network")),
    m_params({})
{
    // TODO: move add_run_command_params to the ctor
    add_run_command_params(m_app, m_params);
}

hailo_status RunCommand::execute()
{
    // TOOD: move implement here
    return run_command(m_params);
}
