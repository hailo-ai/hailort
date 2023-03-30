/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_runner.cpp
 * @brief Run network on hailo device
 **/

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/async_thread.hpp"
#include "common/file_utils.hpp"
#include "common/latency_meter.hpp"

#include "network_runner.hpp"


using namespace hailort;


class SignalEventScopeGuard final
{
public:
    SignalEventScopeGuard(Event &event) : m_event(event)
    {}

    ~SignalEventScopeGuard()
    {
        m_event.signal();
    }

    Event &m_event;
};


//TODO: duplicated
static hailo_status wait_for_threads(std::vector<AsyncThreadPtr<hailo_status>> &threads)
{
    auto last_error_status = HAILO_SUCCESS;
    for (auto &thread : threads) {
        auto thread_status = thread->get();
        if ((HAILO_SUCCESS != thread_status) && (HAILO_STREAM_ABORTED_BY_USER != thread_status)) {
            last_error_status = thread_status;
            LOGGER__ERROR("Thread failed with with status {}", thread_status);
        }
    }
    return last_error_status;
}

VStreamParams::VStreamParams() : name(), params(HailoRTDefaults::get_vstreams_params())
{
}

NetworkParams::NetworkParams() : hef_path(), net_group_name(), vstream_params(), scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN),
    batch_size(HAILO_DEFAULT_BATCH_SIZE), scheduler_threshold(0), scheduler_timeout_ms(0), framerate(UNLIMITED_FRAMERATE), measure_hw_latency(false),
    measure_overall_latency(false)
{
}

NetworkRunner::NetworkRunner(const NetworkParams &params, const std::string &name,
    std::vector<InputVStream> &&input_vstreams, std::vector<OutputVStream> &&output_vstreams,
    std::shared_ptr<ConfiguredNetworkGroup> cng, LatencyMeterPtr overall_latency_meter)
    : m_params(params), m_name(name), m_input_vstreams(std::move(input_vstreams)),
      m_output_vstreams(std::move(output_vstreams)), m_cng(cng), m_overall_latency_meter(overall_latency_meter)
{
}

Expected<std::shared_ptr<NetworkRunner>> NetworkRunner::create_shared(VDevice &vdevice, const NetworkParams &params)
{
    auto hef = Hef::create(params.hef_path);
    CHECK_EXPECTED(hef);

    // Get NG's name if single
    auto net_group_name = params.net_group_name;
    if (net_group_name.empty()) {
        auto net_groups_names = hef->get_network_groups_names();
        CHECK_AS_EXPECTED(net_groups_names.size() == 1, HAILO_INVALID_ARGUMENT, "HEF {} doesn't contain a single NetworkGroup. Pass --name", params.hef_path);
        net_group_name = net_groups_names[0];
    }

    auto cfg_params = vdevice.create_configure_params(hef.value(), net_group_name);
    CHECK_EXPECTED(cfg_params);
    cfg_params->batch_size = params.batch_size;
    if (params.measure_hw_latency) {
        cfg_params->latency |= HAILO_LATENCY_MEASURE;
    }
    auto cfgr_net_groups = vdevice.configure(hef.value(), {{net_group_name, cfg_params.value()}});
    CHECK_EXPECTED(cfgr_net_groups);
    assert(1 == cfgr_net_groups->size());
    auto cfgr_net_group = cfgr_net_groups.value()[0];

    if (HAILO_SCHEDULING_ALGORITHM_NONE!= params.scheduling_algorithm) {
        CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_threshold(params.scheduler_threshold));
        CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_timeout(std::chrono::milliseconds(params.scheduler_timeout_ms)));
        CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_priority(params.scheduler_priority));
    }

    std::map<std::string, hailo_vstream_params_t> vstreams_params;
    for (auto &vstream_params : params.vstream_params) {
        vstreams_params.emplace(vstream_params.name, vstream_params.params);
    }
    auto vstreams = create_vstreams(*cfgr_net_group, vstreams_params);
    CHECK_EXPECTED(vstreams);

    LatencyMeterPtr overall_latency_meter = nullptr;
    if (params.measure_overall_latency) {
        CHECK_AS_EXPECTED((1 == vstreams->first.size()), HAILO_INVALID_OPERATION,
            "Overall latency measurement over multiple inputs network is not supported");

        std::set<std::string> output_names;
        for (auto &output_vstream : vstreams->second) {
            output_names.insert(output_vstream.name());
        }

        overall_latency_meter = make_shared_nothrow<LatencyMeter>(output_names, OVERALL_LATENCY_TIMESTAMPS_LIST_LENGTH);
        CHECK_NOT_NULL_AS_EXPECTED(overall_latency_meter, HAILO_OUT_OF_HOST_MEMORY);
    }
    auto net_runner = make_shared_nothrow<NetworkRunner>(params, net_group_name, std::move(vstreams->first),
        std::move(vstreams->second), cfgr_net_group, overall_latency_meter);
    CHECK_NOT_NULL_AS_EXPECTED(net_runner, HAILO_OUT_OF_HOST_MEMORY);
    return net_runner;
}

Expected<BufferPtr> NetworkRunner::create_dataset_from_input_file(const std::string &file_path,
    const InputVStream &input_vstream)
{
    auto buffer = read_binary_file(file_path);
    CHECK_EXPECTED(buffer);
    CHECK_AS_EXPECTED(0 == (buffer->size() % input_vstream.get_frame_size()), HAILO_INVALID_ARGUMENT,
        "Input file ({}) size {} must be a multiple of the frame size {} ({})",
        file_path, buffer->size(), input_vstream.get_frame_size(), input_vstream.name());

    auto buffer_ptr = make_shared_nothrow<Buffer>(buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_ptr;
}


Expected<BufferPtr> NetworkRunner::create_constant_dataset(const InputVStream &input_vstream)
{
    const uint8_t const_byte = 0xAB;
    auto constant_buffer = Buffer::create_shared(input_vstream.get_frame_size(), const_byte);
    CHECK_EXPECTED(constant_buffer);

    return constant_buffer.release();
}

hailo_status NetworkRunner::run_input_vstream(InputVStream &vstream, Event &shutdown_event, BufferPtr dataset,
    LatencyMeterPtr overall_latency_meter)
{
    auto signal_event_scope_guard = SignalEventScopeGuard(shutdown_event);

    auto last_write_time = std::chrono::steady_clock::now();
    auto framerate_interval = std::chrono::duration<double>(1) / m_params.framerate;
    size_t buffer_offset = 0;
    while(true) {
        if (overall_latency_meter) {
            overall_latency_meter->add_start_sample(std::chrono::steady_clock::now().time_since_epoch());
        }
        auto status = vstream.write(MemoryView((dataset->data() + buffer_offset), vstream.get_frame_size()));
        if (status == HAILO_STREAM_ABORTED_BY_USER) {
            return status;
        }
        CHECK_SUCCESS(status);
        buffer_offset += vstream.get_frame_size();
        buffer_offset %= dataset->size();

        if (m_params.framerate != UNLIMITED_FRAMERATE) {
            auto elapsed_time = std::chrono::steady_clock::now() - last_write_time;
            std::this_thread::sleep_for(framerate_interval - elapsed_time);
            last_write_time = std::chrono::steady_clock::now();
        }
    }
    return HAILO_SUCCESS;
}

hailo_status NetworkRunner::run_output_vstream(OutputVStream &vstream, bool first, std::shared_ptr<NetworkLiveTrack> net_live_track,
    Event &shutdown_event, LatencyMeterPtr overall_latency_meter)
{
    auto signal_event_scope_guard = SignalEventScopeGuard(shutdown_event);

    auto result = Buffer::create(vstream.get_frame_size());
    CHECK_EXPECTED_AS_STATUS(result);
    while(true) {
        auto status = vstream.read(MemoryView(result.value()));
        if (status == HAILO_STREAM_ABORTED_BY_USER) {
            return status;
        }
        CHECK_SUCCESS(status);
        if (overall_latency_meter) {
            overall_latency_meter->add_end_sample(vstream.name(), std::chrono::steady_clock::now().time_since_epoch());
        }
        if (first) {
            net_live_track->progress();
        }
    }
    return HAILO_SUCCESS;
}

hailo_status NetworkRunner::run(Event &shutdown_event, LivePrinter &live_printer, Barrier &barrier)
{
    auto ang = std::unique_ptr<ActivatedNetworkGroup>(nullptr);
    if (HAILO_SCHEDULING_ALGORITHM_NONE == m_params.scheduling_algorithm) {
        auto ang_exp = m_cng->activate();
        if (!ang_exp) {
            barrier.terminate();
        }
        CHECK_EXPECTED_AS_STATUS(ang_exp);
        ang = ang_exp.release();
    }

    auto net_live_track = std::make_shared<NetworkLiveTrack>(m_name, m_cng, m_overall_latency_meter);
    live_printer.add(net_live_track, 1); //support progress over multiple outputs
    barrier.arrive_and_wait();

    std::vector<AsyncThreadPtr<hailo_status>> threads;
    for (auto &input_vstream : m_input_vstreams) {
        BufferPtr dataset = nullptr;
        for (auto &params : m_params.vstream_params) {
            if ((input_vstream.name() == params.name) && (!params.input_file_path.empty())) {
                auto dataset_exp = create_dataset_from_input_file(params.input_file_path, input_vstream);
                CHECK_EXPECTED_AS_STATUS(dataset_exp);
                dataset = dataset_exp.release();
            }
        }
        if (nullptr == dataset) {
            auto dataset_exp = create_constant_dataset(input_vstream);
            CHECK_EXPECTED_AS_STATUS(dataset_exp);
            dataset = dataset_exp.release();
        }

        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("SEND", [this, &input_vstream, &shutdown_event,
            dataset](){
            return run_input_vstream(input_vstream, shutdown_event, dataset, m_overall_latency_meter);
        }));
    }

    bool first = true; //TODO: check with multiple outputs
    for (auto &output_vstream : m_output_vstreams) {
        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("RECV", [this, &output_vstream, first, net_live_track,
        &shutdown_event](){
            return run_output_vstream(output_vstream, first, net_live_track, shutdown_event, m_overall_latency_meter);
        }));
        first = false;
    }

    //TODO: return threads and move stop outside?
    CHECK_SUCCESS(shutdown_event.wait(HAILO_INFINITE_TIMEOUT));
    stop();
    return wait_for_threads(threads);
}

void NetworkRunner::stop()
{
    for (auto &input_vstream : m_input_vstreams) {
        (void) input_vstream.abort();
    }
    for (auto &output_vstream : m_output_vstreams) {
        (void) output_vstream.abort();
    }
}

Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> NetworkRunner::create_vstreams(
    ConfiguredNetworkGroup &net_group, const std::map<std::string, hailo_vstream_params_t> &params)
{//TODO: support network name
    size_t match_count = 0;

    std::map<std::string, hailo_vstream_params_t> input_vstreams_params;
    auto input_vstreams_info = net_group.get_input_vstream_infos();
    CHECK_EXPECTED(input_vstreams_info);
    for (auto &input_vstream_info : input_vstreams_info.value()) {
        auto elem_it = params.find(input_vstream_info.name);
        if (elem_it != params.end()) {
            input_vstreams_params.emplace(input_vstream_info.name, elem_it->second);
            match_count++;
        }
        else {
            input_vstreams_params.emplace(input_vstream_info.name, HailoRTDefaults::get_vstreams_params());
        }
    }

    std::map<std::string, hailo_vstream_params_t> output_vstreams_params;
    auto output_vstreams_info = net_group.get_output_vstream_infos();
    CHECK_EXPECTED(output_vstreams_info);
    for (auto &output_vstream_info : output_vstreams_info.value()) {
        auto elem_it = params.find(output_vstream_info.name);
        if (elem_it != params.end()) {
            output_vstreams_params.emplace(output_vstream_info.name, elem_it->second);
            match_count++;
        }
        else {
            output_vstreams_params.emplace(output_vstream_info.name, HailoRTDefaults::get_vstreams_params());
        }
    }

    CHECK(match_count == params.size(), make_unexpected(HAILO_INVALID_ARGUMENT), "One of the params has an invalid vStream name");

    auto input_vstreams = VStreamsBuilder::create_input_vstreams(net_group, input_vstreams_params);
    CHECK_EXPECTED(input_vstreams);

    auto output_vstreams = VStreamsBuilder::create_output_vstreams(net_group, output_vstreams_params);
    CHECK_EXPECTED(output_vstreams);

    return {{input_vstreams.release(), output_vstreams.release()}};//TODO: move? copy elision?
}