/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "temp_measurement.hpp"
#include "graph_printer.hpp"
#if defined(__GNUC__)
// TODO: Support on windows (HRT-5919)
#include "download_action_list_command.hpp"
#endif
#include "common.hpp"

#include "common/file_utils.hpp"
#include "common/async_thread.hpp"
#include "common/barrier.hpp"
#include "common/latency_meter.hpp"
#include "common/filesystem.hpp"
#include "hailo/network_group.hpp"
#include "hailo/hef.hpp"
#include "hailo/vstream.hpp"
#include "hailo/vdevice.hpp"

#include <vector>
#include <algorithm>
#include <signal.h>
#include <condition_variable>
std::condition_variable wait_for_exit_cv;

/* The SIGUSR1 and SIGUSR2 signals are set aside for you to use any way you want.
    They’re useful for simple interprocess communication. */
#define USER_SIGNAL (SIGUSR1)

constexpr size_t OVERALL_LATENCY_TIMESTAMPS_LIST_LENGTH (512);
constexpr uint32_t DEFAULT_TIME_TO_RUN_SECONDS = 5;
#ifndef HAILO_EMULATOR
constexpr std::chrono::milliseconds TIME_TO_WAIT_FOR_CONFIG(300);
#define HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS (HAILO_DEFAULT_VSTREAM_TIMEOUT_MS)
#else /* ifndef HAILO_EMULATOR */
constexpr std::chrono::milliseconds TIME_TO_WAIT_FOR_CONFIG(30000);
#define HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS (HAILO_DEFAULT_VSTREAM_TIMEOUT_MS * 100)
#endif /* ifndef HAILO_EMULATOR */

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
    CHECK(prev_handler !=  SIG_ERR, HAILO_INVALID_OPERATION, "signal failed, errno = {}", errno);
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

static void add_run_command_params(CLI::App *run_subcommand, inference_runner_params& params)
{
    // TODO: init values in RunCommand ctor
    params.measure_latency = false;
    params.measure_overall_latency = false;
    params.power_measurement.measure_power = false;
    params.power_measurement.measure_current = false;
    params.show_progress = true;
    params.time_to_run = 0;
    params.frames_count = 0;
    params.measure_temp = false;

    add_device_options(run_subcommand, params.device_params);
    add_vdevice_options(run_subcommand, params.device_params);

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
        "This batch applies to the whole network_group. for differential batch per network, see --net-batch-size")
        ->check(CLI::PositiveNumber)
        ->default_val(HAILO_DEFAULT_BATCH_SIZE);

    run_subcommand->add_option("--net-batch-size", params.batch_per_network,
        "Inference batch per network (network names can be found using parse-hef command).\n"
        "In case of multiple networks, usage is as follows: --net-batch-size <network_name_1>=<batch_size_1> --net-batch-size <network_name_2>=<batch_size_2>")
        ->check(NetworkBatchMap)
        ->excludes(total_batch_size)
        ->excludes(frames_count);

    run_subcommand->add_option("--power-mode", params.power_mode,
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
    CLI::Option *measure_power_opt = run_subcommand->add_flag("--measure-power",
        params.power_measurement.measure_power, "Measure power consumption");
    CLI::Option *measure_current_opt = run_subcommand->add_flag("--measure-current",
        params.power_measurement.measure_current, "Measure current")->excludes(measure_power_opt);
    measure_power_opt->excludes(measure_current_opt);

    run_subcommand->add_flag("--show-progress,!--dont-show-progress", params.show_progress,
        "Show inference progress")
        ->default_val("true");

    auto transformation_group = run_subcommand->add_option_group("Transformations");
    transformation_group->add_option("--quantized", params.transform.quantized,
        "true means the tool assumes that the data is already quantized,\n"
        "false means it is the tool's responsability to quantize (scale) the data.")
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
    measure_stats_subcommand->add_flag("--elem-fps", params.pipeline_stats.measure_elem_fps,
        "Measure the fps of each pipeline element separately");
    measure_stats_subcommand->add_flag("--elem-latency", params.pipeline_stats.measure_elem_latency,
        "Measure the latency of each pipeline element separately")
        ->group(""); // --elem-latency will be hidden in the --help print.
    measure_stats_subcommand->add_flag("--elem-queue-size", params.pipeline_stats.measure_elem_queue_size,
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

    // TODO: Support on windows (HRT-5919)
    #if defined(__GNUC__)
    auto *collect_runtime_data_subcommand = run_subcommand->add_subcommand("collect-runtime-data",
        "Collect runtime data to be used by the Profiler");
    static const char *JSON_SUFFIX = ".json";
    collect_runtime_data_subcommand->add_option("--output-path",
        params.runtime_data.runtime_data_output_path, "Runtime data output file path")
        ->default_val("runtime_data.json")
        ->check(FileSuffixValidator(JSON_SUFFIX));
    static const uint32_t DEFAULT_BATCH_TO_MEASURE = 2;
    collect_runtime_data_subcommand->add_option("--batch-to-measure",
        params.runtime_data.batch_to_measure, "Batch to be measured")
        ->default_val(DEFAULT_BATCH_TO_MEASURE);
    collect_runtime_data_subcommand->parse_complete_callback([&params]() {
        // If this subcommand was parsed, then we need to download runtime_data
        params.runtime_data.collect_runtime_data = true;
    });
    #endif

    auto measure_power_group = run_subcommand->add_option_group("Measure Power/Current");
    CLI::Option *power_sampling_period = measure_power_group->add_option("--sampling-period",
        params.power_measurement.sampling_period, "Sampling Period");
    CLI::Option *power_averaging_factor = measure_power_group->add_option("--averaging-factor",
        params.power_measurement.averaging_factor, "Averaging Factor");
    PowerMeasurementSubcommand::init_sampling_period_option(power_sampling_period);
    PowerMeasurementSubcommand::init_averaging_factor_option(power_averaging_factor);

    run_subcommand->add_flag("--measure-temp", params.measure_temp, "Measure chip temperature");

    run_subcommand->parse_complete_callback([&params, hef_new, power_sampling_period,
            power_averaging_factor, measure_power_opt, measure_current_opt]() {
        PARSE_CHECK(!hef_new->empty(), "Single HEF file/directory is required");
        bool is_hw_only = InferMode::HW_ONLY == params.mode;
        params.transform.transform = (!is_hw_only || (params.inputs_name_and_file_path.size() > 0));
        PARSE_CHECK((!params.transform.quantized || (HAILO_FORMAT_TYPE_AUTO == params.transform.format_type)),
            "User data type must be auto when quantized is set");
        bool has_oneof_measure_flags = (!measure_power_opt->empty() || !measure_current_opt->empty());
        PARSE_CHECK(power_sampling_period->empty() || has_oneof_measure_flags,
            "--sampling-period requires --measure-power or --measure-current");
        PARSE_CHECK(power_averaging_factor->empty() || has_oneof_measure_flags,
            "--averaging-period factor --measure-power or --measure-current");
        PARSE_CHECK(((0 != params.time_to_run) || (0 == (params.frames_count % params.batch_size))),
            "--batch-size should be a divisor of --frames-count if provided");
        // TODO HRT-5363 support multiple devices
        PARSE_CHECK((params.device_params.vdevice_params.device_count == 1) || params.csv_output.empty() ||
            !(params.power_measurement.measure_power || params.power_measurement.measure_current || params.measure_temp),
            "Writing measurements in csv format is not supported for multiple devices");

        PARSE_CHECK(("*" != params.device_params.pcie_params.pcie_bdf),
            "Passing '*' as BDF is not supported for 'run' command. for multiple devices inference see '--device-count'");

        if ((0 == params.time_to_run) && (0 == params.frames_count)) {
            // Use default
            params.time_to_run = DEFAULT_TIME_TO_RUN_SECONDS;
        }

        if (params.runtime_data.collect_runtime_data) {
            if ((0 != params.frames_count) && (params.frames_count < params.runtime_data.batch_to_measure)) {
                LOGGER__WARNING("--frames-count ({}) is smaller than --batch-to-measure ({}), "
                    "hence timestamps will not be updated in runtime data", params.frames_count,
                    params.runtime_data.batch_to_measure);
            }
        }
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
    std::map<std::string, Buffer> &input_dataset, Barrier &barrier, LatencyMeter &overall_latency_meter, uint16_t batch_size)
{
    assert(input_dataset.find(send_object.name()) != input_dataset.end());
    const Buffer &input_buffer = input_dataset.at(send_object.name());
    assert((input_buffer.size() % send_object.get_frame_size()) == 0);
    const size_t frames_in_buffer = input_buffer.size() / send_object.get_frame_size();
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
                const_cast<uint8_t*>(input_buffer.data()) + offset,
                send_object.get_frame_size()));
            if (HAILO_STREAM_INTERNAL_ABORT == status) {
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
    std::map<std::string, Buffer> &dst_data, std::atomic_size_t &received_frames_count, uint32_t output_idx, bool show_progress,
    uint16_t batch_size)
{
    uint32_t num_of_batches = ((0 == params.time_to_run) ? (params.frames_count / batch_size) : UINT32_MAX);
    for (size_t i = 0; i < num_of_batches; i++) {
        if (params.measure_latency) {
            barrier.arrive_and_wait();
        }
        for (int j = 0; j < batch_size; j++) {
            auto status = recv_object.read(MemoryView(dst_data[recv_object.name()]));
            if (HAILO_SUCCESS != status) {
                return status;
            }

            if (params.measure_overall_latency) {
                overall_latency_meter.add_end_sample(output_idx, std::chrono::steady_clock::now().time_since_epoch());
            }

            if (show_progress && params.show_progress) {
                progress_bar->make_progress();
            }
            received_frames_count++;
        }
    }
    return HAILO_SUCCESS;
}

hailo_status abort_low_level_streams(ConfiguredNetworkGroup &configured_net_group, const std::string &network_name = "")
{
    // If network_name is not given, all networks are addressed
    auto status = HAILO_SUCCESS; // Best effort
    auto input_streams = configured_net_group.get_input_streams_by_network(network_name);
    CHECK_EXPECTED_AS_STATUS(input_streams);
    for (auto &input_stream : input_streams.release()) {
        auto abort_status = input_stream.get().abort();
        if (HAILO_SUCCESS != abort_status) {
            LOGGER__ERROR("Failed to abort input stream {}", input_stream.get().name());
            status = abort_status;
        }
    }
    auto output_streams = configured_net_group.get_output_streams_by_network(network_name);
    CHECK_EXPECTED_AS_STATUS(output_streams);
    for (auto &output_stream : output_streams.release()) {
        auto abort_status = output_stream.get().abort();
        if (HAILO_SUCCESS != abort_status) {
            LOGGER__ERROR("Failed to abort output stream {}", output_stream.get().name());
            status = abort_status;
        }
    }
    return status;
}

Expected<std::map<std::string, std::vector<InputVStream>>> create_input_vstreams(ConfiguredNetworkGroup &configured_net_group,
    const inference_runner_params &params)
{
    std::map<std::string, std::vector<InputVStream>> res;
    auto network_infos = configured_net_group.get_network_infos();
    CHECK_EXPECTED(network_infos);
    for (auto &network_info : network_infos.value()) {
        auto input_vstreams_params = configured_net_group.make_input_vstream_params(params.transform.quantized,
            params.transform.format_type, HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_info.name);
        CHECK_EXPECTED(input_vstreams_params);

        for (auto &vstream_params : input_vstreams_params.value()) {
            vstream_params.second.pipeline_elements_stats_flags = inference_runner_params_to_pipeline_elem_stats_flags(params.pipeline_stats);
            vstream_params.second.vstream_stats_flags = inference_runner_params_to_vstream_stats_flags(params.pipeline_stats);
        }
        auto input_vstreams = VStreamsBuilder::create_input_vstreams(configured_net_group, input_vstreams_params.value());
        CHECK_EXPECTED(input_vstreams);
        res.emplace(network_info.name, input_vstreams.release());
    }
    return res;
}

Expected<std::map<std::string, std::vector<OutputVStream>>> create_output_vstreams(ConfiguredNetworkGroup &configured_net_group,
    const inference_runner_params &params)
{
    std::map<std::string, std::vector<OutputVStream>> res;
    auto network_infos = configured_net_group.get_network_infos();
    CHECK_EXPECTED(network_infos);
    for (auto &network_info : network_infos.value()) {
        auto output_vstreams_params = configured_net_group.make_output_vstream_params(params.transform.quantized,
            params.transform.format_type, HAILORTCLI_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_info.name);
        CHECK_EXPECTED(output_vstreams_params);

        for (auto &vstream_params : output_vstreams_params.value()) {
            vstream_params.second.pipeline_elements_stats_flags = inference_runner_params_to_pipeline_elem_stats_flags(params.pipeline_stats);
            vstream_params.second.vstream_stats_flags = inference_runner_params_to_vstream_stats_flags(params.pipeline_stats);
        }
        auto output_vstreams = VStreamsBuilder::create_output_vstreams(configured_net_group, output_vstreams_params.value());
        CHECK_EXPECTED(output_vstreams);
        res.emplace(network_info.name, output_vstreams.release());
    }
    return res;
}

Expected<std::map<std::string, std::vector<std::reference_wrapper<InputStream>>>> create_input_streams(ConfiguredNetworkGroup &configured_net_group)
{
    std::map<std::string, std::vector<std::reference_wrapper<InputStream>>> res;
    auto network_infos = configured_net_group.get_network_infos();
    CHECK_EXPECTED(network_infos);
    for (auto &network_info : network_infos.value()) {
        auto input_streams = configured_net_group.get_input_streams_by_network(network_info.name);
        CHECK_EXPECTED(input_streams);
        res.emplace(network_info.name, input_streams.release());
    }
    return res;
}

Expected<std::map<std::string, std::vector<std::reference_wrapper<OutputStream>>>> create_output_streams(ConfiguredNetworkGroup &configured_net_group)
{
    std::map<std::string, std::vector<std::reference_wrapper<OutputStream>>> res;
    auto network_infos = configured_net_group.get_network_infos();
    CHECK_EXPECTED(network_infos);
    for (auto &network_info : network_infos.value()) {
        auto output_streams = configured_net_group.get_output_streams_by_network(network_info.name);
        CHECK_EXPECTED(output_streams);
        res.emplace(network_info.name, output_streams.release());
    }
    return res;
}

// TODO: HRT-5177 create output buffers inside run_streaming
template< typename RecvObject>
Expected<std::map<std::string, Buffer>> create_output_buffers(
    std::map<std::string, std::vector<std::reference_wrapper<RecvObject>>> &recv_objects_per_network)
{
    std::map<std::string, Buffer> dst_data;
    for (auto &recv_objects : recv_objects_per_network) {
        for (auto &recv_object : recv_objects.second) {
            auto buffer = Buffer::create(recv_object.get().get_frame_size());
            CHECK_EXPECTED(buffer);
            dst_data[recv_object.get().name()] = buffer.release();
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
    /* params.batch_per_network is a partial list of networks.
       If a network is not in it, it gets the network_group_batch (params.batch_size) */
    for (auto &name_to_batch_str : params.batch_per_network) {
        auto name_to_batch = get_network_to_batch(name_to_batch_str);
        if (network_name == name_to_batch.first) {
            return name_to_batch.second;
        }
    }
    return params.batch_size;
}

Expected<std::map<std::string, ConfigureNetworkParams>> get_configure_params(const inference_runner_params &params, hailort::Hef &hef, hailo_stream_interface_t interface)
{
    std::map<std::string, ConfigureNetworkParams> configure_params = {};

    hailo_configure_params_t config_params = {};
    hailo_status status = hailo_init_configure_params(reinterpret_cast<hailo_hef>(&hef), interface, &config_params);
    CHECK_SUCCESS_AS_EXPECTED(status);

    // TODO: SDK-14842, for now this function supports only one network_group
    /* params.batch_per_network is a partial list of networks.
       If a network is not in it, it gets the network_group_batch (params.batch_size) */
    if (params.batch_per_network.empty()) {
        config_params.network_group_params[0].batch_size = params.batch_size;
    } else {
        for (auto &name_to_batch_str : params.batch_per_network) {
            auto name_to_batch = get_network_to_batch(name_to_batch_str);
            auto network_name = name_to_batch.first;
            auto batch_size = name_to_batch.second;
            bool found = false;
            for (uint8_t network_idx = 0; network_idx < config_params.network_group_params[0].network_params_by_name_count; network_idx++) {
                if (0 == strcmp(network_name.c_str(), config_params.network_group_params[0].network_params_by_name[network_idx].name)) {
                    config_params.network_group_params[0].network_params_by_name[network_idx].network_params.batch_size = batch_size;
                    found = true;
                }
            }
            CHECK_AS_EXPECTED(found, HAILO_INVALID_ARGUMENT, "Did not find any network named {}. Use 'parse-hef' option to see network names.",
                network_name);
        }
    }
    config_params.network_group_params[0].power_mode = params.power_mode;
    configure_params.emplace(std::string(config_params.network_group_params[0].name),
        ConfigureNetworkParams(config_params.network_group_params[0]));

    if (params.measure_latency) {
        configure_params[std::string(config_params.network_group_params[0].name)].latency |= HAILO_LATENCY_MEASURE;
    }

    return configure_params;
}

template<typename SendObject, typename RecvObject>
static hailo_status run_streaming_impl(ConfiguredNetworkGroup &configured_net_group,
    std::map<std::string, Buffer> &input_dataset,
    std::map<std::string, Buffer> &output_buffers,
    const inference_runner_params &params,
    std::vector<std::reference_wrapper<SendObject>> &send_objects,
    std::vector<std::reference_wrapper<RecvObject>> &recv_objects,
    const std::string network_name,
    InferProgress &network_group_progress_bar,
    NetworkInferResult &inference_result)
{
    // latency resources init
    if (params.measure_overall_latency) {
        CHECK((send_objects.size() == 1), HAILO_INVALID_OPERATION, "Overall latency measurement not support multiple inputs network");
    }
    std::set<uint32_t> output_channels;
    for (uint32_t output_channel_index = 0; output_channel_index < recv_objects.size(); output_channel_index++) {
        output_channels.insert(output_channel_index);
    }

    LatencyMeter overall_latency_meter(output_channels, OVERALL_LATENCY_TIMESTAMPS_LIST_LENGTH);
    Barrier barrier(send_objects.size() + recv_objects.size());

    std::vector<std::atomic_size_t> frames_recieved_per_output(recv_objects.size());
    for (auto &count : frames_recieved_per_output) {
        count = 0;
    }
    auto batch_size = get_batch_size(params, network_name);

    std::vector<AsyncThreadPtr<hailo_status>> results;

    auto progress_bar_exp = network_group_progress_bar.create_network_progress_bar(network_name);
    CHECK_EXPECTED_AS_STATUS(progress_bar_exp);
    auto progress_bar = progress_bar_exp.release();
    const auto start = std::chrono::high_resolution_clock::now();

    // Launch async read/writes
    uint32_t output_index = 0;
    auto first = true;
    for (auto& recv_object : recv_objects) {
        auto &frames_recieved = frames_recieved_per_output[output_index];
        results.emplace_back(std::make_unique<AsyncThread<hailo_status>>(
            [progress_bar, params, &recv_object, &output_buffers, first, &barrier, &overall_latency_meter,
            &frames_recieved, output_index, batch_size]() {
                auto res = recv_loop(params, recv_object.get(), progress_bar, barrier, overall_latency_meter,
                    output_buffers, frames_recieved, output_index, first, batch_size);
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
        results.emplace_back(std::make_unique<AsyncThread<hailo_status>>(
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

        status = abort_low_level_streams(configured_net_group, network_name);
        barrier.terminate();
        CHECK_SUCCESS(status);
    }

    // Wait for all results
    auto error_status = HAILO_SUCCESS;
    for (auto& result : results) {
        auto status = result->get();
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
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

    auto network_input_streams = configured_net_group.get_input_streams_by_network(network_name);
    CHECK_EXPECTED_AS_STATUS(network_input_streams);
    inference_result.m_total_send_frame_size = total_send_frame_size(network_input_streams.value());
    auto network_output_streams = configured_net_group.get_output_streams_by_network(network_name);
    CHECK_EXPECTED_AS_STATUS(network_output_streams);
    inference_result.m_total_recv_frame_size = total_recv_frame_size(network_output_streams.value());

    if (params.measure_latency) {
        if (auto hw_latency = configured_net_group.get_latency_measurement(network_name)) {
            auto hw_latency_p = make_unique_nothrow<std::chrono::nanoseconds>(hw_latency->avg_hw_latency);
            CHECK_NOT_NULL(hw_latency_p, HAILO_OUT_OF_HOST_MEMORY);
            inference_result.m_hw_latency = std::move(hw_latency_p);
        }
    } else {
        inference_result.m_infer_duration = std::make_unique<double>(std::chrono::duration<double>(end - start).count());
    }

    if (params.measure_overall_latency) {
        auto overall_latency = overall_latency_meter.get_latency(true);
        CHECK_EXPECTED_AS_STATUS(overall_latency);
        inference_result.m_overall_latency = std::make_unique<std::chrono::nanoseconds>(*overall_latency);
    }

    return HAILO_SUCCESS;
}

template<typename SendObject, typename RecvObject>
static Expected<NetworkGroupInferResult> run_streaming(ConfiguredNetworkGroup &configured_net_group,
    std::map<std::string, Buffer> &input_dataset,
    std::map<std::string, Buffer> &output_buffers,
    const inference_runner_params &params,
    std::map<std::string, std::vector<std::reference_wrapper<SendObject>>> &send_objects_per_network,
    std::map<std::string, std::vector<std::reference_wrapper<RecvObject>>> &recv_objects_per_network)
{
    CHECK_AS_EXPECTED(send_objects_per_network.size() == recv_objects_per_network.size(), HAILO_INTERNAL_FAILURE,
        "Not all networks was parsed correctly.");

    // TODO: support AsyncThreadPtr for Expected, and use it instead of status
    std::vector<AsyncThreadPtr<hailo_status>> networks_threads_status;
    networks_threads_status.reserve(send_objects_per_network.size());
    std::map<std::string, NetworkInferResult> networks_results;

    // TODO (HRT-5789): instead of init this map and giving it to run_streaming_impl, change AsyncThread to return Expected
    for (auto &network_name_pair : send_objects_per_network) {
        networks_results.emplace(network_name_pair.first, NetworkInferResult());
    }

    InferProgress network_group_progress_bar(configured_net_group, params, std::chrono::seconds(1));

    if (params.show_progress) {
        network_group_progress_bar.start();
    }

    for (auto &network_name_pair : send_objects_per_network) {
        CHECK_AS_EXPECTED(contains(recv_objects_per_network, network_name_pair.first), HAILO_INTERNAL_FAILURE,
            "Not all networks was parsed correctly.");
        auto network_name = network_name_pair.first;
        networks_threads_status.emplace_back(std::make_unique<AsyncThread<hailo_status>>(
            [&configured_net_group, &input_dataset, &output_buffers, &params, &send_objects_per_network, &recv_objects_per_network, network_name,
            &network_group_progress_bar, &networks_results]() {
                return run_streaming_impl(configured_net_group, input_dataset, output_buffers, params,
                send_objects_per_network.at(network_name),
                recv_objects_per_network.at(network_name),
                network_name, network_group_progress_bar, networks_results.at(network_name));
            }
        ));
    }

    // Wait for all results
    for (auto& status : networks_threads_status) {
        auto network_status = status->get();
        CHECK_SUCCESS_AS_EXPECTED(network_status);
    }

    if (params.show_progress) {
        network_group_progress_bar.finish();
    }

    // Update final_result struct - with all inferences results
    NetworkGroupInferResult final_result(std::move(networks_results));

    if (should_measure_pipeline_stats(params)) {
        final_result.update_pipeline_stats(send_objects_per_network, recv_objects_per_network);
    }

    return final_result;
}

static Expected<NetworkGroupInferResult> run_inference(ConfiguredNetworkGroup &configured_net_group,
    std::map<std::string, Buffer> &input_dataset,
    const inference_runner_params &params)
{
    switch (params.mode) {
    case InferMode::STREAMING:
    {
        auto in_vstreams = create_input_vstreams(configured_net_group, params);
        CHECK_EXPECTED(in_vstreams);

        auto out_vstreams = create_output_vstreams(configured_net_group, params);
        CHECK_EXPECTED(out_vstreams);

        // run_streaming function should get reference_wrappers to vstreams instead of the instances themselves
        std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>> input_refs_map;
        for (auto &input_vstreams_per_network : in_vstreams.value()) {
            std::vector<std::reference_wrapper<InputVStream>> input_refs;
            for (auto &input_vstream : input_vstreams_per_network.second) {
                input_refs.emplace_back(input_vstream);
            }
            input_refs_map.emplace(input_vstreams_per_network.first, input_refs);
        }
        std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>> output_refs_map;
        for (auto &output_vstreams_per_network : out_vstreams.value()) {
            std::vector<std::reference_wrapper<OutputVStream>> output_refs;
            for (auto &output_vstream : output_vstreams_per_network.second) {
                output_refs.emplace_back(output_vstream);
            }
            output_refs_map.emplace(output_vstreams_per_network.first, output_refs);
        }

        auto output_buffers = create_output_buffers(output_refs_map);
        CHECK_EXPECTED(output_buffers);

        auto res = run_streaming<InputVStream, OutputVStream>(configured_net_group, input_dataset,
            output_buffers.value(), params, input_refs_map, output_refs_map);

        if (!params.dot_output.empty()) {
            const auto status = GraphPrinter::write_dot_file(in_vstreams.value(), out_vstreams.value(), params.hef_path,
                params.dot_output, should_measure_pipeline_stats(params));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }

        // Note: In VStreams d'tor, low-level-streams clears their abort flag. In low-level-streams d'tor, 'flush()' is called.
        //       In order to avoid error logs on 'flush()', we re-set the abort flag in the low-level streams after vstreams d'tor.
        // TODO: HRT-5177 fix that note
        in_vstreams->clear();
        out_vstreams->clear();

        if (0 < params.time_to_run) {
            auto status = abort_low_level_streams(configured_net_group);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
        CHECK_EXPECTED(res);
        return res;

    }
    case InferMode::HW_ONLY:
    {
        auto input_streams = create_input_streams(configured_net_group);
        CHECK_EXPECTED(input_streams);
        auto output_streams = create_output_streams(configured_net_group);
        CHECK_EXPECTED(output_streams);

        auto output_buffers = create_output_buffers(output_streams.value());
        CHECK_EXPECTED(output_buffers);

        return run_streaming<InputStream, OutputStream>(configured_net_group, input_dataset, output_buffers.value(),
            params, input_streams.value(), output_streams.value());
    }
    default:
        return make_unexpected(HAILO_INVALID_OPERATION);
    }
}

static Expected<std::unique_ptr<ActivatedNetworkGroup>> activate_network_group(ConfiguredNetworkGroup &network_group)
{
    hailo_activate_network_group_params_t network_group_params = {};
    auto activated_network_group = network_group.activate(network_group_params);
    CHECK_EXPECTED(activated_network_group, "Failed activating network group");

    // Wait for configuration
    // TODO: HRT-2492 wait for config in a normal way
    std::this_thread::sleep_for(TIME_TO_WAIT_FOR_CONFIG);

    return activated_network_group;
}

static Expected<std::map<std::string, Buffer>> create_constant_dataset(
    const std::vector<std::reference_wrapper<InputStream>> &input_streams, const hailo_transform_params_t &trans_params)
{
    const uint8_t const_byte = 0xAB;
    std::map<std::string, Buffer> dataset;
    for (const auto &input_stream : input_streams) {
        const auto frame_size = hailo_get_host_frame_size(&(input_stream.get().get_info()), &trans_params);
        auto constant_buffer = Buffer::create(frame_size, const_byte);
        if (!constant_buffer) {
            std::cerr << "Out of memory, tried to allocate " << frame_size << std::endl;
            return make_unexpected(constant_buffer.status());
        }

        dataset.emplace(input_stream.get().name(), constant_buffer.release());
    }

    return dataset;
}

static Expected<std::map<std::string, Buffer>> create_dataset_from_files(
    const std::vector<std::reference_wrapper<InputStream>> &input_streams, const std::vector<std::string> &input_files,
    const hailo_transform_params_t &trans_params, InferMode mode)
{
    CHECK_AS_EXPECTED(input_streams.size() == input_files.size(), HAILO_INVALID_ARGUMENT, "Number of input files ({}) must be equal to the number of inputs ({})", input_files.size(), input_streams.size());

    std::map<std::string, std::string> file_paths;
    if ((input_streams.size() == 1) && (input_files[0].find("=") == std::string::npos)) { // Legacy single input format
        file_paths.emplace(input_streams[0].get().name(), input_files[0]);
    }
    else {
        file_paths = format_strings_to_key_value_pairs(input_files);
    }

    std::map<std::string, Buffer> dataset;
    for (const auto &input_stream : input_streams) {
        const auto host_frame_size = hailo_get_host_frame_size(&(input_stream.get().get_info()), &trans_params);
        const auto stream_name = std::string(input_stream.get().name());
        CHECK_AS_EXPECTED(stream_name.find("=") == std::string::npos, HAILO_INVALID_ARGUMENT, "stream inputs must not contain '=' characters: {}", stream_name);

        const auto file_path_it = file_paths.find(stream_name);
        CHECK_AS_EXPECTED(file_paths.end() != file_path_it, HAILO_INVALID_ARGUMENT, "Missing input file for input: {}", stream_name);
        
        auto host_buffer = read_binary_file(file_path_it->second);
        CHECK_EXPECTED(host_buffer, "Failed reading file {}", file_path_it->second);
        CHECK_AS_EXPECTED((host_buffer->size() % host_frame_size) == 0, HAILO_INVALID_ARGUMENT,
            "Input file ({}) size {} must be a multiple of the frame size {} ({})", file_path_it->second, host_buffer->size(), host_frame_size, stream_name);

        if (InferMode::HW_ONLY == mode) {
            const size_t frames_count = (host_buffer->size() / host_frame_size);
            const size_t hw_frame_size = input_stream.get().get_frame_size();
            const size_t hw_buffer_size = frames_count * hw_frame_size;
            auto hw_buffer = Buffer::create(hw_buffer_size);
            CHECK_EXPECTED(hw_buffer);

            auto transform_context = InputTransformContext::create(input_stream.get().get_info(), trans_params);
            CHECK_EXPECTED(transform_context);
            
            for (size_t i = 0; i < frames_count; i++) {
                MemoryView host_data(static_cast<uint8_t*>(host_buffer->data() + (i*host_frame_size)), host_frame_size);
                MemoryView hw_data(static_cast<uint8_t*>(hw_buffer->data() + (i*hw_frame_size)), hw_frame_size);

                auto status = transform_context.value()->transform(host_data, hw_data);
                CHECK_SUCCESS_AS_EXPECTED(status);
            }
            dataset[stream_name] = hw_buffer.release();
        }
        else {
            dataset[stream_name] = host_buffer.release();
        }
    }

    return dataset;
}

static Expected<std::map<std::string, Buffer>> create_dataset(
    const std::vector<std::reference_wrapper<InputStream>> &input_streams,
    const inference_runner_params &params)
{
    hailo_transform_params_t trans_params = {};
    trans_params.transform_mode = (params.transform.transform ? HAILO_STREAM_TRANSFORM_COPY : HAILO_STREAM_NO_TRANSFORM);
    trans_params.user_buffer_format.order = HAILO_FORMAT_ORDER_AUTO;
    trans_params.user_buffer_format.flags = (params.transform.quantized ? HAILO_FORMAT_FLAGS_QUANTIZED : HAILO_FORMAT_FLAGS_NONE);
    trans_params.user_buffer_format.type = params.transform.format_type;

    if (!params.inputs_name_and_file_path.empty()) {
        return create_dataset_from_files(input_streams, params.inputs_name_and_file_path, trans_params, params.mode);
    }
    else {
        return create_constant_dataset(input_streams, trans_params);
    }
}

Expected<NetworkGroupInferResult> activate_network_group_and_run(
    Device &device,
    std::shared_ptr<ConfiguredNetworkGroup> network_group,
    const inference_runner_params &params)
{
    auto activated_net_group = activate_network_group(*network_group);
    CHECK_EXPECTED(activated_net_group, "Failed activate network_group");

    auto input_streams = network_group->get_input_streams();
    auto input_dataset = create_dataset(input_streams, params);
    CHECK_EXPECTED(input_dataset, "Failed creating input dataset");

    hailo_power_measurement_types_t measurement_type = HAILO_POWER_MEASUREMENT_TYPES__MAX_ENUM;
    bool should_measure_power = false;
    if (params.power_measurement.measure_power) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__POWER;
        should_measure_power = true;
    } else if (params.power_measurement.measure_current) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__CURRENT;
        should_measure_power = true;
    }

    std::unique_ptr<LongPowerMeasurement> long_power_measurement = nullptr;
    if (should_measure_power) {
        auto long_power_measurement_exp = PowerMeasurementSubcommand::start_power_measurement(device,
            HAILO_DVM_OPTIONS_AUTO,
            measurement_type, params.power_measurement.sampling_period, params.power_measurement.averaging_factor);
        CHECK_EXPECTED(long_power_measurement_exp);
        long_power_measurement = make_unique_nothrow<LongPowerMeasurement>(long_power_measurement_exp.release());
        CHECK_NOT_NULL_AS_EXPECTED(long_power_measurement, HAILO_OUT_OF_HOST_MEMORY);
    }

    bool should_measure_temp = params.measure_temp;
    TemperatureMeasurement temp_measure(device);
    if (should_measure_temp) {
        auto status = temp_measure.start_measurement();
        CHECK_SUCCESS_AS_EXPECTED(status, "Failed to get chip's temperature");
    }

    auto infer_result = run_inference(*network_group, input_dataset.value(), params);
    CHECK_EXPECTED(infer_result, "Error failed running inference");

    NetworkGroupInferResult inference_result(infer_result.release());
    std::vector<std::reference_wrapper<Device>> device_refs;
    device_refs.push_back(device);
    inference_result.initialize_measurements(device_refs);

    if (should_measure_power) {
        auto status = long_power_measurement->stop();
        CHECK_SUCCESS_AS_EXPECTED(status);

        if (params.power_measurement.measure_current) {
            status = inference_result.set_current_measurement(device.get_dev_id(), std::move(long_power_measurement));
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            status = inference_result.set_power_measurement(device.get_dev_id(), std::move(long_power_measurement));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }

    if (should_measure_temp) {
        temp_measure.stop_measurement();
        auto temp_measure_p = make_unique_nothrow<TempMeasurementData>(temp_measure.get_data());
        CHECK_NOT_NULL_AS_EXPECTED(temp_measure_p, HAILO_OUT_OF_HOST_MEMORY);
        auto status = inference_result.set_temp_measurement(device.get_dev_id(), std::move(temp_measure_p));
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    return inference_result;
}

Expected<NetworkGroupInferResult> run_command_hef_single_device(const inference_runner_params &params)
{
    auto device = create_device(params.device_params);
    CHECK_EXPECTED(device, "Failed creating device");

    auto hef = Hef::create(params.hef_path.c_str());
    CHECK_EXPECTED(hef, "Failed reading hef file {}", params.hef_path);

    auto interface = device.value()->get_default_streams_interface();
    CHECK_EXPECTED(interface, "Failed to get default streams interface");

    auto configure_params = get_configure_params(params, hef.value(), interface.value());
    CHECK_EXPECTED(configure_params);

    auto network_group_list = device.value()->configure(hef.value(), configure_params.value());
    CHECK_EXPECTED(network_group_list, "Failed configure device from hef");

    #if defined(__GNUC__)
    // TODO: Support on windows (HRT-5919)
    if (params.runtime_data.collect_runtime_data) {
        DownloadActionListCommand::set_batch_to_measure(*device.value(), params.runtime_data.batch_to_measure);
    }
    #endif

    // TODO: SDK-14842, for now this function supports only one network_group
    auto network_group = network_group_list.value()[0];
    auto inference_result = activate_network_group_and_run(*device.value().get(), network_group, params);

    #if defined(__GNUC__)
    // TODO: Support on windows (HRT-5919)
    if (params.runtime_data.collect_runtime_data) {
        if ((0 == params.frames_count) && inference_result) {
            const auto frames_count = inference_result->frames_count();
            if (frames_count && (frames_count.value() <  params.runtime_data.batch_to_measure)) {
                LOGGER__WARNING("Number of frames sent ({}) is smaller than --batch-to-measure ({}), "
                    "hence timestamps will not be updated in runtime data", frames_count.value(),
                    params.runtime_data.batch_to_measure);
            }
        }

        DownloadActionListCommand::execute(*device.value(), params.runtime_data.runtime_data_output_path,
            network_group_list.value(), params.hef_path);
    }
    #endif
    CHECK_EXPECTED(inference_result);
    return inference_result;
}

Expected<NetworkGroupInferResult> run_command_hef_vdevice(const inference_runner_params &params)
{
    auto hef = Hef::create(params.hef_path.c_str());
    CHECK_EXPECTED(hef, "Failed reading hef file {}", params.hef_path);

    hailo_vdevice_params_t vdevice_params = {};
    vdevice_params.device_count = params.device_params.vdevice_params.device_count;
    auto vdevice = VDevice::create(vdevice_params);
    CHECK_EXPECTED(vdevice, "Failed creating vdevice");

    // VDevice always has Pcie devices
    auto configure_params = get_configure_params(params, hef.value(), hailo_stream_interface_t::HAILO_STREAM_INTERFACE_PCIE);
    CHECK_EXPECTED(configure_params);

    auto network_group_list = vdevice.value()->configure(hef.value(), configure_params.value());
    CHECK_EXPECTED(network_group_list, "Failed configure vdevice from hef");

    // TODO: SDK-14842, for now this function supports only one network_group
    auto network_group = network_group_list.value()[0];
    auto activated_net_group = activate_network_group(*network_group);
    CHECK_EXPECTED(activated_net_group, "Failed activate network_group");

    auto input_streams = network_group->get_input_streams();
    auto input_dataset = create_dataset(input_streams, params);
    CHECK_EXPECTED(input_dataset, "Failed creating input dataset");

    hailo_power_measurement_types_t measurement_type = HAILO_POWER_MEASUREMENT_TYPES__MAX_ENUM;
    bool should_measure_power = false;
    if (params.power_measurement.measure_power) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__POWER;
        should_measure_power = true;
    } else if (params.power_measurement.measure_current) {
        measurement_type = HAILO_POWER_MEASUREMENT_TYPES__CURRENT;
        should_measure_power = true;
    }

    auto physical_devices = vdevice.value()->get_physical_devices();
    CHECK_EXPECTED(physical_devices);

    std::map<std::string, std::unique_ptr<LongPowerMeasurement>> power_measurements;
    if (should_measure_power) {
        for (auto &device : physical_devices.value()) {
            auto long_power_measurement_exp = PowerMeasurementSubcommand::start_power_measurement(device,
                HAILO_DVM_OPTIONS_AUTO,
                measurement_type, params.power_measurement.sampling_period, params.power_measurement.averaging_factor);
            CHECK_EXPECTED(long_power_measurement_exp, "Failed starting power measurement on device {}", device.get().get_dev_id());
            auto long_power_measurement_p = make_unique_nothrow<LongPowerMeasurement>(long_power_measurement_exp.release());
            CHECK_NOT_NULL_AS_EXPECTED(long_power_measurement_p, HAILO_OUT_OF_HOST_MEMORY);
            power_measurements.emplace(device.get().get_dev_id(), std::move(long_power_measurement_p));
        }
    }

    std::map<std::string, std::unique_ptr<TemperatureMeasurement>> temp_measurements;
    if (params.measure_temp) {
        for (auto &device : physical_devices.value()) {
            auto temp_measure = make_unique_nothrow<TemperatureMeasurement>(device);
            CHECK_NOT_NULL_AS_EXPECTED(temp_measure, HAILO_OUT_OF_HOST_MEMORY);
            auto status = temp_measure->start_measurement();
            CHECK_SUCCESS_AS_EXPECTED(status, "Failed starting temperature measurement on device {}", device.get().get_dev_id());
            temp_measurements.emplace(device.get().get_dev_id(), std::move(temp_measure));
        }
    }

    auto infer_result = run_inference(*network_group, input_dataset.value(), params);
    CHECK_EXPECTED(infer_result, "Error failed running inference");

    NetworkGroupInferResult inference_result(infer_result.release());
    inference_result.initialize_measurements(physical_devices.value());

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
    }

    if (params.measure_temp) {
        for(const auto &temp_measure_pair : temp_measurements) {
            temp_measure_pair.second->stop_measurement();
            auto temp_measure_p = make_unique_nothrow<TempMeasurementData>(temp_measure_pair.second->get_data());
            CHECK_NOT_NULL_AS_EXPECTED(temp_measure_p, HAILO_OUT_OF_HOST_MEMORY);
            auto status = inference_result.set_temp_measurement(temp_measure_pair.first, std::move(temp_measure_p));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }

    return inference_result;
}

Expected<NetworkGroupInferResult> run_command_hef(const inference_runner_params &params)
{
    if (params.device_params.vdevice_params.device_count > 1) {
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

    const auto files = Filesystem::get_files_in_dir_flat(hef_dir);
    CHECK_EXPECTED_AS_STATUS(files);

    for (const auto &full_path : files.value()) {
        if (Filesystem::has_suffix(full_path, ".hef")) {
            contains_hef = true;
            curr_params.hef_path = full_path;
            std::cout << std::string(80, '*') << std::endl << "Inferring " << full_path << ":"<< std::endl;
            auto infer_stats = run_command_hef(curr_params);
            printer.print(full_path, infer_stats);

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
    auto printer = InferStatsPrinter::create(params);
    CHECK_EXPECTED_AS_STATUS(printer, "Failed to initialize infer stats printer");
    if (!params.csv_output.empty()) {
        printer->print_csv_header();
    }

    auto is_dir = Filesystem::is_directory(params.hef_path.c_str());
    CHECK_EXPECTED_AS_STATUS(is_dir, "Failed checking if path is directory");

    if (is_dir.value()){
        return run_command_hefs_dir(params, printer.value());
    } else {
        auto infer_stats = run_command_hef(params);
        // TODO: pass here network name without .hef
        printer->print(params.hef_path, infer_stats);
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
