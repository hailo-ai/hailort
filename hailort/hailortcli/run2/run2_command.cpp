/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file run2_command.cpp
 * @brief Run inference on hailo device
 **/

#include "run2_command.hpp"
#include "live_printer.hpp"
#include "timer_live_track.hpp"
#include "measurement_live_track.hpp"
#include "network_runner.hpp"

#include "common/barrier.hpp"
#include "common/async_thread.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/hef.hpp"

#include <memory>
#include <vector>

using namespace hailort;

constexpr uint32_t DEFAULT_TIME_TO_RUN_SECONDS = 5;

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

/** VStreamApp */
class VStreamApp : public CLI::App
{
public:
    VStreamApp(const std::string &description, const std::string &name, CLI::Option *hef_path_option, CLI::Option *net_group_name_option);
    const VStreamParams& get_params();

private:
    CLI::Option* add_flag_callback(CLI::App *app, const std::string &name, const std::string &description,
        std::function<void(bool)> function);

    VStreamParams m_params;
};

VStreamApp::VStreamApp(const std::string &description, const std::string &name, CLI::Option *hef_path_option,
    CLI::Option *net_group_name_option) : CLI::App(description, name), m_params()
{
    add_option("name", m_params.name, "vStream name")
        ->check(VStreamNameValidator(hef_path_option, net_group_name_option));

    add_option("--input-file", m_params.input_file_path,
        "Input file path. If not given, random data will be used. File format should be raw binary data with size that is a factor of the input shape size")
        ->default_val("");

    auto format_opt_group = add_option_group("Format");
    format_opt_group->add_option("--type", m_params.params.user_buffer_format.type, "Format type")
        ->transform(HailoCheckedTransformer<hailo_format_type_t>({
            { "auto", HAILO_FORMAT_TYPE_AUTO },
            { "uint8", HAILO_FORMAT_TYPE_UINT8 },
            { "uint16", HAILO_FORMAT_TYPE_UINT16 },
            { "float32", HAILO_FORMAT_TYPE_FLOAT32 }
        }))
        ->default_val("auto");

    format_opt_group->add_option("--order", m_params.params.user_buffer_format.order, "Format order")
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
            { "hailo_nms", HAILO_FORMAT_ORDER_HAILO_NMS },
            { "nchw", HAILO_FORMAT_ORDER_NCHW },
            { "yuy2", HAILO_FORMAT_ORDER_YUY2 },
            { "nv12", HAILO_FORMAT_ORDER_NV12 },
            { "nv21", HAILO_FORMAT_ORDER_NV21 },
            { "rgb4", HAILO_FORMAT_ORDER_RGB4 },
            { "i420", HAILO_FORMAT_ORDER_I420 }
        }))
        ->default_val("auto");

    add_flag_callback(format_opt_group, "-q,--quantized,!--no-quantized", "Whether or not data is quantized",
        [this](bool result){
            m_params.params.user_buffer_format.flags = result ?
                static_cast<hailo_format_flags_t>(m_params.params.user_buffer_format.flags | HAILO_FORMAT_FLAGS_QUANTIZED) :
                static_cast<hailo_format_flags_t>(m_params.params.user_buffer_format.flags & (~HAILO_FORMAT_FLAGS_QUANTIZED));})
        ->run_callback_for_default()
        ->default_val(true); // default_val() must be after run_callback_for_default()
}

const VStreamParams& VStreamApp::get_params()
{
    //TODO: instead of copy do a move + call reset()? change func name to move_params? same for NetworkParams/NetworkApp class
    return m_params;
}

CLI::Option* VStreamApp::add_flag_callback(CLI::App *app, const std::string &name, const std::string &description,
        std::function<void(bool)> function)
    {
        // get_option doesn't support multiple names so taking the first one
        auto first_name = name.substr(0, name.find(','));
        auto wrap_function = [app, function, first_name](std::int64_t){function(app->get_option(first_name)->as<bool>());};
        return app->add_flag_function(name, wrap_function, description);
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
class NetworkApp : public CLI::App
{
public:
    NetworkApp(const std::string &description, const std::string &name);
    const NetworkParams& get_params();

private:
    void add_vstream_app_subcom(CLI::Option *hef_path_option, CLI::Option *net_group_name_option);
    NetworkParams m_params;
};

NetworkApp::NetworkApp(const std::string &description, const std::string &name) : CLI::App(description, name), m_params()
{
    auto hef_path_option = add_option("hef", m_params.hef_path, "HEF file path")->check(CLI::ExistingFile);
    auto net_group_name_option = add_option("--name", m_params.net_group_name, "Network group name")
        ->default_val("")
        ->needs(hef_path_option)
        ->check(NetworkGroupNameValidator(hef_path_option));
    // NOTE: callbacks/params aren't called/updated before auto-completion (even after changing the order in App.hpp - at least for 2 jumps)
    auto net_params = add_option_group("Network Group Parameters");
    net_params->add_option("--batch-size", m_params.batch_size, "Batch size")->default_val(HAILO_DEFAULT_BATCH_SIZE);
    net_params->add_option("--scheduler-threshold", m_params.scheduler_threshold, "Scheduler threshold")->default_val(0);
    net_params->add_option("--scheduler-timeout", m_params.scheduler_timeout_ms, "Scheduler timeout in milliseconds")->default_val(0);
    net_params->add_option("--scheduler-priority", m_params.scheduler_priority, "Scheduler priority")->default_val(HAILO_SCHEDULER_PRIORITY_NORMAL);

    auto run_params = add_option_group("Run Parameters");
    run_params->add_option("--framerate", m_params.framerate, "Input vStreams framerate")->default_val(UNLIMITED_FRAMERATE);

    // TODO: support multiple scheduling algorithms
    m_params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN;

    add_vstream_app_subcom(hef_path_option, net_group_name_option);
}

void NetworkApp::add_vstream_app_subcom(CLI::Option *hef_path_option, CLI::Option *net_group_name_option)
{
    auto vstream_app = std::make_shared<VStreamApp>("Set vStream", "set-vstream", hef_path_option, net_group_name_option);
    vstream_app->immediate_callback();
    vstream_app->callback([this, vstream_app, hef_path_option, net_group_name_option]() {
        m_params.vstream_params.push_back(vstream_app->get_params());

        // Throw an error if anything is left over and should not be.
        _process_extras();

        // NOTE: calling "net_app->clear(); m_params = NetworkParams();" is not sufficient because default values
        //         need to be re-set. we can override clear and reset them but there might be other issues as well
        //         and this one feels less hacky ATM
        remove_subcommand(vstream_app.get());
        // Remove from parsed_subcommands_ as well (probably a bug in CLI11)
        parsed_subcommands_.erase(std::remove_if(
            parsed_subcommands_.begin(), parsed_subcommands_.end(),
            [vstream_app](auto x){return x == vstream_app.get();}),
            parsed_subcommands_.end());
        add_vstream_app_subcom(hef_path_option, net_group_name_option);
    });

    // Must set fallthrough to support nested repeated subcommands.
    vstream_app->fallthrough();
    add_subcommand(vstream_app);
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

    const std::vector<NetworkParams>& get_network_params();
    std::chrono::seconds get_time_to_run();
    std::vector<hailo_device_id_t> get_dev_ids();
    uint32_t get_device_count();
    bool get_measure_power();
    bool get_measure_current();
    bool get_measure_temp();
    bool get_multi_process_service();
    const std::string &get_group_id();

    void set_scheduling_algorithm(hailo_scheduling_algorithm_t scheduling_algorithm);
    void set_measure_latency();

private:
    void add_net_app_subcom();
    std::vector<NetworkParams> m_network_params;
    uint32_t m_time_to_run;
    std::vector<std::string> m_device_id;
    uint32_t m_device_count;
    bool m_multi_process_service;
    std::string m_group_id;

    bool m_measure_hw_latency;
    bool m_measure_overall_latency;

    bool m_measure_power;
    bool m_measure_current;
    bool m_measure_temp;
};


Run2::Run2() : CLI::App("Run networks (preview)", "run2")
{
    add_net_app_subcom();
    add_option("-t,--time-to-run", m_time_to_run, "Time to run (seconds)")
        ->default_val(DEFAULT_TIME_TO_RUN_SECONDS)
        ->check(CLI::PositiveNumber);

    auto vdevice_options_group = add_option_group("VDevice Options");

    auto dev_id_opt = vdevice_options_group->add_option("-s,--device-id", m_device_id,
        "Device id, same as returned from `hailortcli scan` command. For multiple devices, use space as separator.");

    vdevice_options_group->add_option("--device-count", m_device_count, "VDevice device count")
        ->default_val(HAILO_DEFAULT_DEVICE_COUNT)
        ->check(CLI::PositiveNumber)
        ->excludes(dev_id_opt);

    vdevice_options_group->add_flag("--multi-process-service", m_multi_process_service, "VDevice multi process service")
        ->default_val(false);

    vdevice_options_group->add_option("--group-id", m_group_id, "VDevice group id")
        ->default_val(HAILO_DEFAULT_VDEVICE_GROUP_ID);

    auto measurement_options_group = add_option_group("Measurement Options");

    auto measure_power_opt = measurement_options_group->add_flag("--measure-power", m_measure_power, "Measure power consumption")
        ->default_val(false);
    
    measurement_options_group->add_flag("--measure-current", m_measure_current, "Measure current")->excludes(measure_power_opt)
        ->default_val(false);

    measurement_options_group->add_flag("--measure-latency", m_measure_hw_latency, "Measure network latency")
        ->default_val(false);
    
    measurement_options_group->add_flag("--measure-overall-latency", m_measure_overall_latency, "Measure overall latency measurement")
        ->default_val(false);

    measurement_options_group->add_flag("--measure-temp", m_measure_temp, "Measure chip temperature")
        ->default_val(false);
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
}

const std::vector<NetworkParams>& Run2::get_network_params()
{
    return m_network_params;
}

std::chrono::seconds Run2::get_time_to_run()
{
    return std::chrono::seconds(m_time_to_run);
}

bool Run2::get_measure_power()
{
    return m_measure_power;
}

bool Run2::get_measure_current()
{
    return m_measure_current;
}

bool Run2::get_measure_temp()
{
    return m_measure_temp;
}

std::vector<hailo_device_id_t> Run2::get_dev_ids()
{
    std::vector<hailo_device_id_t> res;
    res.reserve(m_device_id.size());
    for (auto &id_str : m_device_id) {
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

void Run2::set_scheduling_algorithm(hailo_scheduling_algorithm_t scheduling_algorithm)
{
    for (auto &params: m_network_params) {
        params.scheduling_algorithm = scheduling_algorithm;
    }
}

void Run2::set_measure_latency()
{
    for (auto &params: m_network_params) {
        params.measure_hw_latency = m_measure_hw_latency;
        params.measure_overall_latency = m_measure_overall_latency;
    }
}

bool Run2::get_multi_process_service()
{
    return m_multi_process_service;
}

const std::string &Run2::get_group_id()
{
    return m_group_id;
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
        if ((HAILO_SUCCESS != thread_status) && (HAILO_STREAM_ABORTED_BY_USER != thread_status)) {
            last_error_status = thread_status;
            LOGGER__ERROR("Thread failed with with status {}", thread_status);
        }
    }
    return last_error_status;
}

bool is_valid_ip(const std::string &ip)
{
    int a,b,c,d;
    return (4 == sscanf(ip.c_str(),"%d.%d.%d.%d", &a, &b, &c, &d)) &&
        IS_FIT_IN_UINT8(a) && IS_FIT_IN_UINT8(b) && IS_FIT_IN_UINT8(c) && IS_FIT_IN_UINT8(d);
}

hailo_status Run2Command::execute()
{
    Run2 *app = reinterpret_cast<Run2*>(m_app);

    app->set_measure_latency();

    if (0 == app->get_network_params().size()) {
        LOGGER__ERROR("Nothing to run");
        return HAILO_INVALID_OPERATION;
    }
    if (1 == app->get_network_params().size()) {
        LOGGER__WARN("\"hailortcli run2\" is in preview. It is recommended to use \"hailortcli run\" command for a single network group");
    }

    hailo_vdevice_params_t vdevice_params = {};
    CHECK_SUCCESS(hailo_init_vdevice_params(&vdevice_params));
    auto dev_ids = app->get_dev_ids();
    if (!dev_ids.empty()) {
        vdevice_params.device_count = static_cast<uint32_t>(dev_ids.size());
        vdevice_params.device_ids = dev_ids.data();

        // Disable scheduler for eth VDevice
        if ((1 == dev_ids.size()) && (is_valid_ip(dev_ids[0].id))) {
            vdevice_params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
            CHECK(1 == app->get_network_params().size(), HAILO_INVALID_OPERATION, "On Ethernet inference only one model is allowed");
            app->set_scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_NONE);
        }
    } else {
        vdevice_params.device_count = app->get_device_count();
    }

    vdevice_params.group_id = app->get_group_id().c_str();
    vdevice_params.multi_process_service = app->get_multi_process_service();

    auto vdevice = VDevice::create(vdevice_params);
    CHECK_EXPECTED_AS_STATUS(vdevice);

    // create network runners
    std::vector<std::shared_ptr<NetworkRunner>> net_runners;
    for (auto &net_params : app->get_network_params()) {
        auto net_runner = NetworkRunner::create_shared(*vdevice->get(), net_params);
        CHECK_EXPECTED_AS_STATUS(net_runner);

        net_runners.emplace_back(net_runner.release());
    }
    auto live_printer = std::make_unique<LivePrinter>(std::chrono::seconds(1));

    live_printer->add(std::make_shared<TimerLiveTrack>(app->get_time_to_run()), 0);

    auto shutdown_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED_AS_STATUS(shutdown_event);
    std::vector<AsyncThreadPtr<hailo_status>> threads;
    Barrier barrier(net_runners.size() + 1); // We wait for all nets to finish activation + this thread to start sampling
    for (auto &net_runner : net_runners) {
        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("NG_INFER", [&net_runner, &shutdown_event,
            &live_printer, &barrier](){
            return net_runner->run(shutdown_event.value(), *live_printer, barrier);
        }));
    }

    auto physical_devices = vdevice.value()->get_physical_devices();
    CHECK_EXPECTED_AS_STATUS(physical_devices);

    for (auto &device : physical_devices.value()) {
        auto measurement_live_track = MeasurementLiveTrack::create_shared(device.get(), app->get_measure_power(),
            app->get_measure_current(), app->get_measure_temp());
        CHECK_EXPECTED_AS_STATUS(measurement_live_track);
        live_printer->add(measurement_live_track.release(), 2);
    }

    // TODO: wait for all nets before starting timer. start() should update TimerLiveTrack to start. or maybe append here but first in vector...
    barrier.arrive_and_wait();
    CHECK_SUCCESS(live_printer->start());
    auto status = shutdown_event->wait(app->get_time_to_run());
    if (HAILO_TIMEOUT != status) {
        // if shutdown_event is signaled its because one of the send/recv threads failed
        LOGGER__ERROR("Encountered error during inference. See log for more information.");
    }
    live_printer.reset(); // Ensures that the final print will include real values and not with values of when streams are already aborted.
    shutdown_event->signal();
    return wait_for_threads(threads);
}