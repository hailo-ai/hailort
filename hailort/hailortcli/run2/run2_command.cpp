/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file run2_command.cpp
 * @brief Run inference on hailo device
 **/

#include "run2_command.hpp"
#include "common/utils.hpp"
#include "live_stats.hpp"
#include "timer_live_track.hpp"
#include "measurement_live_track.hpp"
#include "network_runner.hpp"

#include "common/barrier.hpp"
#include "common/async_thread.hpp"
#include "../common.hpp"
#include "hailo/device.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/hef.hpp"
#include "../download_action_list_command.hpp"
#include "common/filesystem.hpp"

#include <functional>
#include <memory>
#include <vector>
#include <regex>

using namespace hailort;

constexpr uint32_t DEFAULT_TIME_TO_RUN_SECONDS = 5;

static const char *JSON_SUFFIX = ".json";
static const char *RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER = "<hef>";
static const std::vector<uint16_t> DEFAULT_BATCH_SIZES = {1, 2, 4, 8, 16};
static const uint32_t RUNTIME_DATA_BATCH_INDEX_TO_MEASURE_DEFAULT = 2;

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;


static std::string get_default_buffer_type() {
    return Filesystem::does_file_exists(std::string(DMA_HEAP_PATH))? "dmabuf":"ptr";
}

/** VStreamNameValidator */
class VStreamNameValidator : public CLI::Validator {
  public:
    VStreamNameValidator(const CLI::Option *hef_path_option, const CLI::Option *net_group_name_option);
private:
    static std::vector<std::string> get_values(const std::string &hef_path, const std::string &net_group_name);
};

VStreamNameValidator::VStreamNameValidator(const CLI::Option *hef_path_option, const CLI::Option *net_group_name_option) : Validator("VSTREAM") {
    func_ = [](std::string&) {
        //TODO: support?
        return std::string();
    };
    autocomplete_func_ = [hef_path_option, net_group_name_option](const std::string&) {
        // TODO: remove existing names from prev user input
        return get_values(hef_path_option->as<std::string>(), net_group_name_option->as<std::string>());
    };
}

std::vector<std::string> VStreamNameValidator::get_values(const std::string &hef_path, const std::string &net_group_name)
{
    auto hef = Hef::create(hef_path);
    if (!hef.has_value()) {
        return {};
    }

    // TODO: duplicate
    auto actual_net_group_name = net_group_name;
    if (actual_net_group_name.empty()) {
        auto net_groups_names = hef->get_network_groups_names();
        if (net_groups_names.size() != 1) {
            return {};
        }
        actual_net_group_name = net_groups_names[0];
    }

    auto vstreams_info = hef->get_all_vstream_infos(actual_net_group_name);
    if (!vstreams_info.has_value()) {
        return {};
    }

    std::vector<std::string> names;
    for (auto &vstream_info : vstreams_info.value()) {
        names.emplace_back(vstream_info.name);
    }
    return names;
}

class StreamNameValidator : public CLI::Validator {
  public:
    StreamNameValidator(const CLI::Option *hef_path_option, const CLI::Option *net_group_name_option);
private:
    static std::vector<std::string> get_values(const std::string &hef_path, const std::string &net_group_name);
};

StreamNameValidator::StreamNameValidator(const CLI::Option *hef_path_option, const CLI::Option *net_group_name_option) : Validator("STREAM") {
    func_ = [](std::string&) {
        //TODO: support?
        return std::string();
    };
    autocomplete_func_ = [hef_path_option, net_group_name_option](const std::string&) {
        // TODO: remove existing names from prev user input
        return get_values(hef_path_option->as<std::string>(), net_group_name_option->as<std::string>());
    };
}

std::vector<std::string> StreamNameValidator::get_values(const std::string &hef_path, const std::string &net_group_name)
{
    auto hef = Hef::create(hef_path);
    if (!hef.has_value()) {
        return {};
    }

    // TODO: duplicate
    auto actual_net_group_name = net_group_name;
    if (actual_net_group_name.empty()) {
        auto net_groups_names = hef->get_network_groups_names();
        if (net_groups_names.size() != 1) {
            return {};
        }
        actual_net_group_name = net_groups_names[0];
    }

    auto streams_info = hef->get_all_stream_infos(actual_net_group_name);
    if (!streams_info.has_value()) {
        return {};
    }

    std::vector<std::string> names;
    for (auto &stream_info : streams_info.value()) {
        names.emplace_back(stream_info.name);
    }
    return names;
}

IoApp::IoApp(const std::string &description, const std::string &name, Type type) :
    CLI::App(description, name),
    m_type(type),
    m_vstream_params(),
    m_stream_params()
{
}

IoApp::Type IoApp::get_type() const
{
    return m_type;
}

const VStreamParams &IoApp::get_vstream_params() const
{
    // TODO: instead of copy do a move + call reset()? change func name to move_params? same for NetworkParams/NetworkApp class
    return m_vstream_params;
}

const StreamParams &IoApp::get_stream_params() const
{
    // TODO: instead of copy do a move + call reset()? change func name to move_params? same for NetworkParams/NetworkApp class
    return m_stream_params;
}

/** VStreamApp */
class VStreamApp : public IoApp
{
public:
    VStreamApp(const std::string &description, const std::string &name, CLI::Option *hef_path_option, CLI::Option *net_group_name_option);

private:
    CLI::Option* add_flag_callback(CLI::App *app, const std::string &name, const std::string &description,
        std::function<void(bool)> function);
};

VStreamApp::VStreamApp(const std::string &description, const std::string &name, CLI::Option *hef_path_option,
                       CLI::Option *net_group_name_option) :
    IoApp(description, name, IoApp::Type::VSTREAM)
{
    add_option("name", m_vstream_params.name, "vStream name")
        ->check(VStreamNameValidator(hef_path_option, net_group_name_option));

    add_option("--input-file", m_vstream_params.input_file_path,
        "Input file path. If not given, random data will be used. File format should be raw binary data with size that is a factor of the input shape size")
        ->default_val("");

    auto format_opt_group = add_option_group("Format");
    format_opt_group->add_option("--type", m_vstream_params.params.user_buffer_format.type, "Format type")
        ->transform(HailoCheckedTransformer<hailo_format_type_t>({
            { "auto", HAILO_FORMAT_TYPE_AUTO },
            { "uint8", HAILO_FORMAT_TYPE_UINT8 },
            { "uint16", HAILO_FORMAT_TYPE_UINT16 },
            { "float32", HAILO_FORMAT_TYPE_FLOAT32 }
        }))
        ->default_val("auto");

    format_opt_group->add_option("--order", m_vstream_params.params.user_buffer_format.order, "Format order")
        ->transform(HailoCheckedTransformer<hailo_format_order_t>({
            { "auto", HAILO_FORMAT_ORDER_AUTO },
            { "nhwc", HAILO_FORMAT_ORDER_NHWC },
            { "nhcw",HAILO_FORMAT_ORDER_NHCW },
            { "fcr", HAILO_FORMAT_ORDER_FCR },
            { "f8cr", HAILO_FORMAT_ORDER_F8CR },
            { "nhw", HAILO_FORMAT_ORDER_NHW },
            { "nc", HAILO_FORMAT_ORDER_NC },
            { "bayer_rgb", HAILO_FORMAT_ORDER_BAYER_RGB },
            { "12_bit_bayer_rgb", HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB },
            { "hailo_nms_by_class", HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS },
            { "hailo_nms_by_score", HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE },
            { "nchw", HAILO_FORMAT_ORDER_NCHW },
            { "yuy2", HAILO_FORMAT_ORDER_YUY2 },
            { "nv12", HAILO_FORMAT_ORDER_NV12 },
            { "nv21", HAILO_FORMAT_ORDER_NV21 },
            { "rgb4", HAILO_FORMAT_ORDER_RGB4 },
            { "i420", HAILO_FORMAT_ORDER_I420 }
        }))
        ->default_val("auto");
}

CLI::Option* VStreamApp::add_flag_callback(CLI::App *app, const std::string &name, const std::string &description,
    std::function<void(bool)> function)
{
    // get_option doesn't support multiple names so taking the first one
    auto first_name = name.substr(0, name.find(','));
    auto wrap_function = [app, function, first_name](std::int64_t){function(app->get_option(first_name)->as<bool>());};
    return app->add_flag_function(name, wrap_function, description);
}

/** StreamApp */
class StreamApp : public IoApp
{
public:
    StreamApp(const std::string &description, const std::string &name, CLI::Option *hef_path_option, CLI::Option *net_group_name_option);
};

StreamApp::StreamApp(const std::string &description, const std::string &name, CLI::Option *hef_path_option,
                     CLI::Option *net_group_name_option) :
    IoApp(description, name, IoApp::Type::STREAM)
{
    add_option("name", m_stream_params.name, "Stream name")
        ->check(StreamNameValidator(hef_path_option, net_group_name_option));

    add_option("--input-file", m_stream_params.input_file_path,
        "Input file path. If not given, random data will be used. File format should be raw binary data with size that is a factor of the input shape size")
        ->default_val("");
}

/** NetworkGroupNameValidator */
class NetworkGroupNameValidator : public CLI::Validator {
  public:
    NetworkGroupNameValidator(const CLI::Option *hef_path_option);
};

NetworkGroupNameValidator::NetworkGroupNameValidator(const CLI::Option *hef_path_option) : Validator("NETWORK_GROUP") {
    func_ = [](std::string&) {
        //TODO: support?
        return std::string();
    };
    autocomplete_func_ = [hef_path_option](const std::string&) -> std::vector<std::string>{
        auto hef = Hef::create(hef_path_option->as<std::string>());
        if (!hef.has_value()) {
            return {};
        }
        return hef->get_network_groups_names();
    };
}

/** NetworkApp */
NetworkApp::NetworkApp(const std::string &description, const std::string &name) :
    CLI::App(description, name),
    m_params()
{
    auto hef_path_option = add_option("hef", m_params.hef_path, "HEF file path")->check(CLI::ExistingFile);
    auto net_group_name_option = add_option("--name", m_params.net_group_name, "Network group name")
        ->default_val("")
        ->needs(hef_path_option)
        ->check(NetworkGroupNameValidator(hef_path_option));
    // NOTE: callbacks/params aren't called/updated before auto-completion (even after changing the order in App.hpp - at least for 2 jumps)
    auto net_params = add_option_group("Network Group Parameters");
    net_params->add_option("--batch-size", m_params.batch_size,
        "Batch size\n"
        "The default value is HAILO_DEFAULT_BATCH_SIZE - which means the batch is determined by HailoRT automatically")
        ->default_val(HAILO_DEFAULT_BATCH_SIZE);
    net_params->add_option("--scheduler-threshold", m_params.scheduler_threshold, "Scheduler threshold")->default_val(0);
    net_params->add_option("--scheduler-timeout", m_params.scheduler_timeout_ms, "Scheduler timeout in milliseconds")->default_val(0);
    net_params->add_option("--scheduler-priority", m_params.scheduler_priority, "Scheduler priority")->default_val(HAILO_SCHEDULER_PRIORITY_NORMAL);

    auto run_params = add_option_group("Run Parameters");
    run_params->add_option("--framerate", m_params.framerate, "Input vStreams framerate")->default_val(UNLIMITED_FRAMERATE);

    auto vstream_subcommand = add_io_app_subcom<VStreamApp>("Set vStream", "set-vstream", hef_path_option, net_group_name_option);
    auto stream_subcommand = add_io_app_subcom<StreamApp>("Set Stream", "set-stream", hef_path_option, net_group_name_option);
    // TODO: doesn't seam to be working (HRT-9886)
    vstream_subcommand->excludes(stream_subcommand);
    stream_subcommand->excludes(vstream_subcommand);
}

const NetworkParams& NetworkApp::get_params()
{
    return m_params;
}

/** Run2 */
class Run2 : public CLI::App
{
public:
    Run2();
    Run2(const NetworkParams &network_params, uint32_t time_to_run) :
        m_time_to_run(time_to_run), m_scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN), m_device_count(1),
        m_multi_process_service(false), m_measure_power(true), m_measure_current(true), m_measure_temp(true),
        m_measure_fw_actions(false)
    {
        m_network_params.push_back(network_params);
    }

    Expected<std::unique_ptr<VDevice>> create_vdevice();
    Expected<std::vector<std::shared_ptr<NetworkRunner>>> init_and_run_net_runners(VDevice &vdevice);
    Expected<std::vector<std::shared_ptr<NetworkRunner>>> init_and_run_net_runners_impl(VDevice &vdevice,
        bool should_print, const std::function<void(std::shared_ptr<MeasurementLiveTrack>)> &measurement_track_handler);


    const std::vector<NetworkParams>& get_network_params();
    std::chrono::seconds get_time_to_run();
    std::vector<hailo_device_id_t> get_dev_ids();
    uint32_t get_device_count();
    bool user_requested_power_measurement();
    bool user_requested_current_measurement();
    bool user_requested_temperature_measurement();
    bool get_measure_hw_latency();
    bool get_measure_overall_latency();
    bool get_multi_process_service();
    bool get_measure_fw_actions();
    std::string get_measure_fw_actions_output_path();
    const std::string &get_group_id();
    InferenceMode get_mode() const;
    const std::string &get_output_json_path();

    void update_network_params();
    void set_measure_by_capabilities(const Device::Capabilities &capabilities);
    void set_batch_size(uint16_t batch_size);

private:
    void add_measure_fw_actions_subcom();
    void add_net_app_subcom();

    bool is_ethernet_device() const;
    void validate_and_set_scheduling_algorithm();
    void validate_mode_supports_service();
    void validate_measurements_supported();

    std::vector<NetworkParams> m_network_params;
    uint32_t m_time_to_run;
    InferenceMode m_mode;
    BufferType m_buffer_type;
    hailo_scheduling_algorithm_t m_scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_MAX_ENUM;
    std::string m_stats_json_path;
    std::vector<std::string> m_device_ids;
    uint32_t m_device_count;
    bool m_multi_process_service;
    std::string m_group_id;

    bool m_measure_hw_latency{false};
    bool m_measure_overall_latency{false};

    bool m_measure_power{false};
    bool m_measure_current{false};
    bool m_measure_temp{false};

    bool m_measure_fw_actions{false};
    std::string m_measure_fw_actions_output_path;
};

Run2::Run2() : CLI::App("Run networks", "run2")
{
    add_measure_fw_actions_subcom();
    add_net_app_subcom();
    add_option("-t,--time-to-run", m_time_to_run, "Time to run (seconds)")
        ->default_val(DEFAULT_TIME_TO_RUN_SECONDS)
        ->check(CLI::PositiveNumber);
    add_option("-m,--mode", m_mode, "Inference mode")
        ->transform(HailoCheckedTransformer<InferenceMode>({
            { "full_sync", InferenceMode::FULL_SYNC },
            { "full_async", InferenceMode::FULL_ASYNC },
            { "raw_sync", InferenceMode::RAW_SYNC },
            { "raw_async", InferenceMode::RAW_ASYNC },
            { "raw_async_single_thread", InferenceMode::RAW_ASYNC_SINGLE_THREAD, OptionVisibility::HIDDEN }
        }))->default_val("full_async");

    add_option("--buffer-type", m_buffer_type, "Buffer type to use for all networks")
        ->transform(HailoCheckedTransformer<BufferType>({
            { "ptr", BufferType::VIEW },
            { "dmabuf", BufferType::DMA_BUFFER }
        }))
        ->default_val(get_default_buffer_type())
        ->check([this](const std::string&) -> std::string {
            if (m_mode != InferenceMode::FULL_ASYNC) {
                return "Buffer type option is only valid in full_async mode";
            }
            return std::string();
        });

    add_option("-j,--json", m_stats_json_path, "If set save statistics as json to the specified path")
        ->default_val("")
        ->check(FileSuffixValidator(JSON_SUFFIX));

    add_option("--scheduling-algorithm", m_scheduling_algorithm, "Scheduling algorithm")
        ->transform(HailoCheckedTransformer<hailo_scheduling_algorithm_t>({
            { "round_robin", HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN },
            { "none", HAILO_SCHEDULING_ALGORITHM_NONE },
        }));

    auto vdevice_options_group = add_option_group("VDevice Options");

    auto dev_id_opt = vdevice_options_group->add_option("-s,--device-id", m_device_ids,
        "Device id, same as returned from `hailortcli scan` command. For multiple devices, use space as separator.");

    vdevice_options_group->add_option("--device-count", m_device_count, "VDevice device count")
        ->default_val(HAILO_DEFAULT_DEVICE_COUNT)
        ->check(CLI::PositiveNumber)
        ->excludes(dev_id_opt);
    vdevice_options_group->add_option("--group-id", m_group_id, "VDevice group id")
        ->default_val(HAILO_DEFAULT_VDEVICE_GROUP_ID);
    vdevice_options_group
        ->add_flag("--multi-process-service", m_multi_process_service,"VDevice multi process service")
        ->default_val(false);

    auto measurement_options_group = add_option_group("Measurement Options");

    auto measure_power_opt = measurement_options_group->add_flag("--measure-power", m_measure_power, "Measure power consumption")
        ->default_val(false);

    measurement_options_group->add_flag("--measure-current", m_measure_current, "Measure current")->excludes(measure_power_opt)
        ->default_val(false);

    measurement_options_group->add_flag("--measure-latency", m_measure_hw_latency, "Measure network latency on the NN core")
        ->default_val(false);

    measurement_options_group->add_flag("--measure-overall-latency", m_measure_overall_latency, "Measure overall latency measurement")
        ->default_val(false);

    measurement_options_group->add_flag("--measure-temp", m_measure_temp, "Measure chip temperature")
        ->default_val(false);

    parse_complete_callback([this]() {
        validate_and_set_scheduling_algorithm();
        validate_mode_supports_service();
        validate_measurements_supported();
    });
}

void Run2::add_measure_fw_actions_subcom()
{
    m_measure_fw_actions = false;
    auto measure_fw_actions_subcommand = std::make_shared<NetworkApp>("Collect runtime data to be used by the Profiler", "measure-fw-actions");
    measure_fw_actions_subcommand->parse_complete_callback([this]() {
        m_measure_fw_actions = true;
    });
    measure_fw_actions_subcommand->add_option("--output-path", m_measure_fw_actions_output_path,
        fmt::format("Runtime data output file path\n'{}' will be replaced with the current running hef", RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER))
        ->default_val(fmt::format("runtime_data_{}.json", RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER))
        ->check(FileSuffixValidator(JSON_SUFFIX));

    measure_fw_actions_subcommand->alias("collect-runtime-data");

    add_subcommand(measure_fw_actions_subcommand);
}

void Run2::add_net_app_subcom()
{
    auto net_app = std::make_shared<NetworkApp>("Set network", "set-net");
    net_app->immediate_callback();
    net_app->callback([this, net_app]() {
        m_network_params.push_back(net_app->get_params());

        // Throw an error if anything is left over and should not be.
        _process_extras();

        // NOTE: calling "net_app->clear(); m_params = NetworkParams();" is not sufficient because default values
        //         need to be re-set. we can override clear and reset them but there might be other issues as well
        //         and this one feels less hacky ATM
        remove_subcommand(net_app.get());
        // Remove from parsed_subcommands_ as well (probably a bug in CLI11)
        parsed_subcommands_.erase(std::remove_if(
            parsed_subcommands_.begin(), parsed_subcommands_.end(),
            [net_app](auto x){return x == net_app.get();}),
            parsed_subcommands_.end());
        add_net_app_subcom();
    });
    // NOTE: fallthrough() is not a must here but it is also not working (causing only a single vstream param
    //   instead of >1). Debug - App.hpp::void _parse(std::vector<std::string> &args)
    add_subcommand(net_app);
    // TODO: set _autocomplete based on m_mode (HRT-9886)
}

const std::vector<NetworkParams>& Run2::get_network_params()
{
    return m_network_params;
}

std::chrono::seconds Run2::get_time_to_run()
{
    return std::chrono::seconds(m_time_to_run);
}

bool Run2::user_requested_power_measurement()
{
    return m_measure_power;
}

bool Run2::user_requested_current_measurement()
{
    return m_measure_current;
}

bool Run2::user_requested_temperature_measurement()
{
    return m_measure_temp;
}

bool Run2::get_measure_hw_latency()
{
    return m_measure_hw_latency;
}

bool Run2::get_measure_overall_latency()
{
    return m_measure_overall_latency;
}

std::vector<hailo_device_id_t> Run2::get_dev_ids()
{
    std::vector<hailo_device_id_t> res;
    res.reserve(m_device_ids.size());
    for (auto &id_str : m_device_ids) {
        hailo_device_id_t id = {};
        std::memset(id.id, 0, sizeof(id.id));
        std::strncpy(id.id, id_str.c_str(), sizeof(id.id) - 1);
        res.push_back(id);
    }
    return res;
}

uint32_t Run2::get_device_count()
{
    return m_device_count;
}

void Run2::update_network_params()
{
    for (auto &params : m_network_params) {
        params.mode = m_mode;
        params.buffer_type = m_buffer_type;
        params.multi_process_service = m_multi_process_service;
        params.measure_hw_latency = m_measure_hw_latency;
        params.measure_overall_latency = m_measure_overall_latency;
        params.scheduling_algorithm = m_scheduling_algorithm;
    }
}

void Run2::set_measure_by_capabilities(const Device::Capabilities &capabilities)
{
    m_measure_power = capabilities.power_measurements;
    m_measure_current = capabilities.current_measurements;
    m_measure_temp = capabilities.temperature_measurements;
}

void Run2::set_batch_size(uint16_t batch_size)
{
    for (auto &params: m_network_params) {
        params.batch_size = batch_size;
    }
}

bool Run2::get_multi_process_service()
{
    return m_multi_process_service;
}

bool Run2::get_measure_fw_actions()
{
    return m_measure_fw_actions;
}

std::string Run2::get_measure_fw_actions_output_path()
{
    return m_measure_fw_actions_output_path;
}

const std::string &Run2::get_group_id()
{
    return m_group_id;
}

InferenceMode Run2::get_mode() const
{
    return m_mode;
}

const std::string &Run2::get_output_json_path()
{
    return m_stats_json_path;
}

static bool is_valid_ip(const std::string &ip)
{
    int a,b,c,d;
    return (4 == sscanf(ip.c_str(),"%d.%d.%d.%d", &a, &b, &c, &d)) &&
        IS_FIT_IN_UINT8(a) && IS_FIT_IN_UINT8(b) && IS_FIT_IN_UINT8(c) && IS_FIT_IN_UINT8(d);
}

bool Run2::is_ethernet_device() const
{
    if (m_device_ids.empty()) {
        // By default, if no device ids are given we don't scan for ethernet devices.
        return false;
    }
    return is_valid_ip(m_device_ids[0]);
}

void Run2::validate_mode_supports_service()
{
    if (m_multi_process_service) {
        PARSE_CHECK(((InferenceMode::FULL_SYNC == m_mode) || (InferenceMode::FULL_ASYNC == m_mode)),
            "When running multi-process, only FULL_SYNC or FULL_ASYNC modes are allowed");
    }
}

void Run2::validate_measurements_supported()
{
    if (is_ethernet_device()) {
        PARSE_CHECK(!m_measure_power, "Power measurement is not supported on Ethernet devices");
        PARSE_CHECK(!m_measure_current, "Current measurement is not supported on Ethernet devices");
        PARSE_CHECK(!m_measure_temp, "Temperature measurement is not supported on Ethernet devices");
    }
}

void Run2::validate_and_set_scheduling_algorithm()
{
    if (m_scheduling_algorithm == HAILO_SCHEDULING_ALGORITHM_NONE) {
        PARSE_CHECK(1 == get_network_params().size(), "When setting --scheduling-algorithm=none only one model is allowed");
    }

    if (get_measure_fw_actions()) {
        PARSE_CHECK((m_scheduling_algorithm == HAILO_SCHEDULING_ALGORITHM_MAX_ENUM) ||
                    (m_scheduling_algorithm == HAILO_SCHEDULING_ALGORITHM_NONE),
                    "When measuring fw actions, only --scheduling-algorithm=none is allowed");
        PARSE_CHECK(1 == get_network_params().size(),
            "Only one model is allowed when measuring fw actions");
        m_scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
    }

    if (HAILO_SCHEDULING_ALGORITHM_MAX_ENUM == m_scheduling_algorithm) {
        // algorithm wasn't passed, using ROUND_ROBIN as default
        m_scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN;
    }
}

/** Run2Command */
Run2Command::Run2Command(CLI::App &parent_app) : Command(parent_app.add_subcommand(std::make_shared<Run2>()))
{
}

static hailo_status wait_for_threads(std::vector<AsyncThreadPtr<hailo_status>> &threads)
{
    auto last_error_status = HAILO_SUCCESS;
    for (auto& thread : threads) {
        auto thread_status = thread->get();
        if ((HAILO_SUCCESS != thread_status) && (HAILO_STREAM_ABORT != thread_status)) {
            last_error_status = thread_status;
            LOGGER__ERROR("Thread failed with status {}", thread_status);
        }
    }
    return last_error_status;
}

std::string get_str_infer_mode(const InferenceMode& infer_mode)
{
    switch(infer_mode){
    case InferenceMode::FULL_SYNC:
        return "full_sync";
    case InferenceMode::FULL_ASYNC:
        return "full_async";
    case InferenceMode::RAW_SYNC:
        return "raw_sync";
    case InferenceMode::RAW_ASYNC:
        return "raw_async";
    case InferenceMode::RAW_ASYNC_SINGLE_THREAD:
        return "raw_async_single_thread";
    }

    return "<Unknown>";
}

// We assume that hef_place_holder_regex is valid
std::string format_measure_fw_actions_output_path(const std::string &base_output_path, const std::string &hef_path,
    const std::string &hef_place_holder_regex = RUNTIME_DATA_OUTPUT_PATH_HEF_PLACE_HOLDER,
    const std::string &hef_suffix = ".hef")
{
    const auto hef_basename = Filesystem::basename(hef_path);
    const auto hef_no_suffix = Filesystem::remove_suffix(hef_basename, hef_suffix);
    return std::regex_replace(base_output_path, std::regex(hef_place_holder_regex), hef_no_suffix);
}

Expected<std::reference_wrapper<Device>> get_single_physical_device(VDevice &vdevice)
{
    TRY(auto physical_devices, vdevice.get_physical_devices());
    CHECK_AS_EXPECTED(1 == physical_devices.size(), HAILO_INVALID_OPERATION,
        "Operation not allowed for multi-device");
    auto &res = physical_devices.at(0);
    return std::move(res);
}

Expected<std::unique_ptr<VDevice>> Run2::create_vdevice()
{
    // hailo_vdevice_params_t is a c-structure that have pointers of device_ids, we must keep reference to the devices
    // object alive until vdevice_params is destructed.
    auto dev_ids = get_dev_ids();

    hailo_vdevice_params_t vdevice_params{};
    CHECK_SUCCESS_AS_EXPECTED(hailo_init_vdevice_params(&vdevice_params));
    if (!dev_ids.empty()) {
        vdevice_params.device_count = static_cast<uint32_t>(dev_ids.size());
        vdevice_params.device_ids = dev_ids.data();
    } else {
        vdevice_params.device_count = get_device_count();
    }

    if (get_measure_fw_actions()) {
        CHECK_AS_EXPECTED(1 == get_network_params().size(), HAILO_INVALID_OPERATION, "Only one model is allowed when collecting runtime data");
        CHECK_AS_EXPECTED(!get_multi_process_service(), HAILO_INVALID_OPERATION, "Collecting runtime data is not supported with multi process service");
        CHECK_AS_EXPECTED(get_device_count() == 1, HAILO_INVALID_OPERATION, "Collecting runtime data is not supported with multi device");
        CHECK_AS_EXPECTED(!(get_measure_hw_latency() || get_measure_overall_latency()), HAILO_INVALID_OPERATION, "Latency measurement is not allowed when collecting runtime data");
        CHECK_AS_EXPECTED((get_mode() == InferenceMode::RAW_SYNC) || (get_mode() == InferenceMode::RAW_ASYNC), HAILO_INVALID_OPERATION,
            "'measure-fw-actions' is only supported with '--mode=raw_sync' or '--mode=raw_async'. Received mode: '{}'", get_str_infer_mode(get_mode()));
    }

    vdevice_params.group_id = get_group_id().c_str();
    vdevice_params.multi_process_service = get_multi_process_service();
    assert(HAILO_SCHEDULING_ALGORITHM_MAX_ENUM != m_scheduling_algorithm);
    vdevice_params.scheduling_algorithm = m_scheduling_algorithm;

    return VDevice::create(vdevice_params);
}

Expected<std::vector<std::shared_ptr<NetworkRunner>>> Run2::init_and_run_net_runners(VDevice &vdevice)
{
    return init_and_run_net_runners_impl(vdevice, true, [](std::shared_ptr<MeasurementLiveTrack>) {});
}

Expected<std::vector<std::shared_ptr<NetworkRunner>>> Run2::init_and_run_net_runners_impl(VDevice &vdevice,
    bool should_print, const std::function<void(std::shared_ptr<MeasurementLiveTrack>)> &measurement_track_handler)
{
    std::vector<std::shared_ptr<NetworkRunner>> net_runners;
    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

    // create network runners
    for (auto &net_params : get_network_params()) {
        TRY(auto net_runner, NetworkRunner::create_shared(vdevice, net_params));
        net_runners.emplace_back(net_runner);
    }

    auto live_stats = std::make_unique<LiveStats>(std::chrono::seconds(1), should_print);

    live_stats->add(std::make_shared<TimerLiveTrack>(get_time_to_run()), 0);

    std::vector<AsyncThreadPtr<hailo_status>> threads;
    Barrier activation_barrier(net_runners.size() + 1); // We wait for all nets to finish activation + this thread to start sampling
    for (auto &net_runner : net_runners) {
        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("NG_INFER", [&net_runner, &shutdown_event,
            &live_stats, &activation_barrier]() {
            return net_runner->run(shutdown_event, *live_stats, activation_barrier);
        }));
    }

    auto signal_event_scope_guard = SignalEventScopeGuard(*shutdown_event);

    activation_barrier.arrive_and_wait();

    if (user_requested_power_measurement() || user_requested_current_measurement() || user_requested_temperature_measurement()) {
        TRY(auto physical_devices, vdevice.get_physical_devices());
        for (auto &device : physical_devices) {
            TRY(auto supported_features, device.get().get_capabilities(), "Failed getting device capabilities");
            CHECK_AS_EXPECTED(!user_requested_power_measurement() || supported_features.power_measurements,
                HAILO_INVALID_OPERATION, "Power measurement not supported. Disable the measure-power option");
            CHECK_AS_EXPECTED(!user_requested_current_measurement() || supported_features.current_measurements,
                HAILO_INVALID_OPERATION, "Current measurement not supported. Disable the measure-current option");
            CHECK_AS_EXPECTED(!user_requested_temperature_measurement() || supported_features.temperature_measurements,
                HAILO_INVALID_OPERATION, "Temperature measurement not supported. Disable the measure-temp option");

            TRY(auto measurement_live_track, MeasurementLiveTrack::create_shared(device.get(),
                user_requested_power_measurement(), user_requested_current_measurement(),
                user_requested_temperature_measurement()));

            live_stats->add(measurement_live_track, 2);
            measurement_track_handler(measurement_live_track);
        }
    }

    CHECK_SUCCESS_AS_EXPECTED(live_stats->start());
    auto status = shutdown_event->wait(get_time_to_run());
    if (HAILO_TIMEOUT != status) {
        // if shutdown_event is signaled its because one of the send/recv threads failed
        LOGGER__ERROR("Encountered error during inference. See log for more information.");
    }
    if (!get_output_json_path().empty()){
        live_stats->dump_stats(get_output_json_path(), get_str_infer_mode(get_mode()));
    }
    TRY(auto fps_per_network, live_stats->get_last_measured_fps_per_network_group());
    for (size_t network_runner_index = 0; network_runner_index < fps_per_network.size(); network_runner_index++) {
        net_runners[network_runner_index]->set_last_measured_fps(fps_per_network[network_runner_index]);
    }
    live_stats.reset(); // Ensures that the final print will include real values and not with values of when streams are already aborted.
    for (auto net_runner : net_runners) {
        net_runner->stop();
    }
    shutdown_event->signal();
    wait_for_threads(threads);
    return net_runners;
}

hailo_status Run2Command::execute()
{
    Run2 *app = reinterpret_cast<Run2*>(m_app);

    app->update_network_params();

    CHECK(0 < app->get_network_params().size(), HAILO_INVALID_OPERATION, "Nothing to run");

    if (app->get_measure_hw_latency() || app->get_measure_overall_latency()) {
        CHECK(1 == app->get_network_params().size(), HAILO_INVALID_OPERATION, "When latency measurement is enabled, only one model is allowed");
        LOGGER__WARNING("Measuring latency; frames are sent one at a time and FPS will not be measured");
    }

    TRY(auto vdevice, app->create_vdevice());
    TRY(auto devices, vdevice->get_physical_devices());

    if ((1 == app->get_network_params().size()) &&
    (HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN == app->get_network_params().begin()->scheduling_algorithm) &&
    (devices[0].get().get_type() == Device::Type::INTEGRATED)) {
        LOGGER__WARNING("\"hailortcli run2\" is not optimized for single model usage. It is recommended to use \"hailortcli run\" command for a single model");
    }

    std::vector<uint16_t> batch_sizes_to_run = { app->get_network_params()[0].batch_size };
    if(app->get_measure_fw_actions() && app->get_network_params()[0].batch_size == HAILO_DEFAULT_BATCH_SIZE) {
        // In case measure-fw-actions is enabled and no batch size was provided - we want to run with batch sizes 1,2,4,8,16
        batch_sizes_to_run = DEFAULT_BATCH_SIZES;
    }

    std::string runtime_data_output_path;
    ordered_json action_list_json;

    if (app->get_measure_fw_actions()) {
        TRY(auto device, get_single_physical_device(*vdevice));
        TRY(action_list_json,
            DownloadActionListCommand::init_json_object(device, app->get_network_params()[0].hef_path));
        runtime_data_output_path = format_measure_fw_actions_output_path(
            app->get_measure_fw_actions_output_path(), app->get_network_params()[0].hef_path);
    }

    uint32_t network_group_index = 0;
    for (auto batch_size : batch_sizes_to_run) {
        if(app->get_measure_fw_actions()) {
            app->set_batch_size(batch_size);
            TRY(auto device, get_single_physical_device(*vdevice));
            auto status = DownloadActionListCommand::set_batch_to_measure(device, RUNTIME_DATA_BATCH_INDEX_TO_MEASURE_DEFAULT);
            CHECK_SUCCESS(status);
        }

        TRY(auto net_runners, app->init_and_run_net_runners(*vdevice));
        if(app->get_measure_fw_actions()) { // Collecting runtime data
            TRY(auto device, get_single_physical_device(*vdevice));
            auto status = DownloadActionListCommand::execute(device, net_runners[0]->get_configured_network_group(),
                batch_size, action_list_json, net_runners[0]->get_last_measured_fps(), network_group_index);
            CHECK_SUCCESS(status);

            network_group_index++;
        }
    }
    if(app->get_measure_fw_actions()) { // In case measure-fw-actions is enabled - write data to JSON file
        CHECK_SUCCESS(DownloadActionListCommand::write_to_json(action_list_json, runtime_data_output_path));
    }
    return HAILO_SUCCESS;
}

hailo_status run2_benchmark(const std::string &hef_path, uint32_t time_to_run)
{
    NetworkApp network_app("Set network", "set-net");
    auto network_params = network_app.get_params();
    network_params.hef_path = hef_path;
    network_params.mode = InferenceMode::FULL_ASYNC;
    Run2 app(network_params, time_to_run);
    TRY(auto vdevice, app.create_vdevice());
    TRY(auto devices, vdevice->get_physical_devices());

    // Measure all supported measurements capabilities of all devices
    Device::Capabilities all_devices_caps{true, true, true};
    auto update_capability = [](bool &is_capable, bool is_supported, const std::string &measurement_name) -> void {
        if (is_capable && !is_supported) {
            is_capable = false;
            std::cerr << fmt::format("Measurement {} is not supported\n", measurement_name);
        }
    };
    for (auto const &device : devices) {
        TRY(auto current_device_caps, device.get().get_capabilities(), "Failed getting device capabilities");
        update_capability(all_devices_caps.power_measurements, current_device_caps.power_measurements, "power");
        update_capability(all_devices_caps.current_measurements, current_device_caps.current_measurements, "current");
        update_capability(all_devices_caps.temperature_measurements, current_device_caps.temperature_measurements,
                          "temperature");
    }
    app.set_measure_by_capabilities(all_devices_caps);

    std::vector<std::shared_ptr<MeasurementLiveTrack>> measurement_tracks;
    TRY(auto net_runners, app.init_and_run_net_runners_impl(*vdevice, false,
        [&measurement_tracks](std::shared_ptr<MeasurementLiveTrack> track) -> void {
            measurement_tracks.push_back(track);
        }));
    std::cout << R"(
=======
Summary
=======
)";
    for (const auto &net_runner : net_runners) {
        std::cout << fmt::format("{}: FPS: {:.2f}\n", net_runner->get_name(), net_runner->get_last_measured_fps());
    }
    for (auto measurement_track : measurement_tracks) {
        auto power_ptr = (all_devices_caps.power_measurements
            ? measurement_track->get_power_measurement() : nullptr);
        auto current_ptr = (all_devices_caps.current_measurements
            ? measurement_track->get_current_measurement() : nullptr);
        auto temp_ptr = (all_devices_caps.temperature_measurements
            ? measurement_track->get_temp_measurement() : nullptr);
        if (power_ptr || current_ptr || temp_ptr) {
            std::cout << fmt::format("{}:\n", measurement_track->get_device_id());
        }
        auto measure_ptrs_names = std::vector<std::pair<std::shared_ptr<BaseMeasurement>, const std::string>>{
            {power_ptr, "power"}, {current_ptr, "current"}, {temp_ptr, "temperature"}};
        for (auto &measure_ptr_name : measure_ptrs_names) {
            if (measure_ptr_name.first) {
                const auto acc = measure_ptr_name.first->get_data();
                TRY(auto acc_mean, acc.mean());
                TRY(auto acc_min, acc.min());
                TRY(auto acc_max, acc.max());
                std::cout << fmt::format("  {}: mean={:.2f} min={:.2f} max={:.2f}\n",
                    measure_ptr_name.second, acc_mean, acc_min, acc_max);
            }
        }
    }
    return HAILO_SUCCESS;
}
