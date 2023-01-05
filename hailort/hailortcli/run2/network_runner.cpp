/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_runner.cpp
 * @brief Run network on hailo device
 **/

#include "network_runner.hpp"
#include "common/async_thread.hpp"
#include "hailort_defaults.hpp" //TODO: not API

using namespace hailort;

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

NetworkRunner::NetworkRunner(const NetworkParams &params, const std::string &name,
    std::vector<InputVStream> &&input_vstreams, std::vector<OutputVStream> &&output_vstreams)
    : m_params(params), m_name(name), m_input_vstreams(std::move(input_vstreams)),
      m_output_vstreams(std::move(output_vstreams))
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

    auto interface = vdevice.get_default_streams_interface();
    CHECK_EXPECTED(interface, "Failed to get default streams interface");

    auto cfg_params = hef->create_configure_params(*interface, net_group_name);
    CHECK_EXPECTED(cfg_params);
    cfg_params->batch_size = params.batch_size;
    auto cfgr_net_groups = vdevice.configure(hef.value(), {{net_group_name, cfg_params.value()}});
    CHECK_EXPECTED(cfgr_net_groups);
    assert(1 == cfgr_net_groups->size());
    auto cfgr_net_group = cfgr_net_groups.value()[0];

    CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_threshold(params.scheduler_threshold));
    CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_timeout(std::chrono::milliseconds(params.scheduler_timeout_ms)));

    std::map<std::string, hailo_vstream_params_t> vstreams_params;
    for (auto &vstream_params : params.vstream_params) {
        vstreams_params.emplace(vstream_params.name, vstream_params.params);
    }
    auto vstreams = create_vstreams(*cfgr_net_group, vstreams_params);
    CHECK_EXPECTED(vstreams);

    auto net_runner = make_shared_nothrow<NetworkRunner>(params, net_group_name, std::move(vstreams->first), std::move(vstreams->second));
    CHECK_NOT_NULL_AS_EXPECTED(net_runner, HAILO_OUT_OF_HOST_MEMORY);
    return net_runner;
}

hailo_status NetworkRunner::run_input_vstream(InputVStream &vstream)
{
    auto dataset = Buffer::create(vstream.get_frame_size(), 0xAB);
    CHECK_EXPECTED_AS_STATUS(dataset);
    auto last_write_time = std::chrono::steady_clock::now();
    auto framerate_interval = std::chrono::duration<double>(1) / m_params.framerate;
    while(true) {
        auto status = vstream.write(MemoryView(dataset.value()));
        if (status == HAILO_STREAM_ABORTED_BY_USER) {
            return status;
        }
        CHECK_SUCCESS(status);

        if (m_params.framerate != UNLIMITED_FRAMERATE) {
            auto elapsed_time = std::chrono::steady_clock::now() - last_write_time;
            std::this_thread::sleep_for(framerate_interval - elapsed_time);
            last_write_time = std::chrono::steady_clock::now();
        }
    }
    return HAILO_SUCCESS;
}

hailo_status NetworkRunner::run_output_vstream(OutputVStream &vstream, bool first, std::shared_ptr<NetworkLiveTrack> net_live_track)
{
    auto result = Buffer::create(vstream.get_frame_size());
    CHECK_EXPECTED_AS_STATUS(result);
    while(true) {
        auto status = vstream.read(MemoryView(result.value()));
        if (status == HAILO_STREAM_ABORTED_BY_USER) {
            return status;
        }
        CHECK_SUCCESS(status);
        if (first) {
            net_live_track->progress();
        }
    }
    return HAILO_SUCCESS;
}

hailo_status NetworkRunner::run(Event &shutdown_event, LivePrinter &live_printer)
{
    std::vector<AsyncThreadPtr<hailo_status>> threads;
    for (auto &input_vstream : m_input_vstreams) {
        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>([this, &input_vstream](){
            return run_input_vstream(input_vstream);
        }));
    }

    auto net_live_track = std::make_shared<NetworkLiveTrack>(m_name);
    live_printer.add(net_live_track);//support progress over multiple outputs

    bool first = true;//TODO: check with multiple outputs
    for (auto &output_vstream : m_output_vstreams) {
        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>([&output_vstream, first, net_live_track](){
            return run_output_vstream(output_vstream, first, net_live_track);
        }));
        first = false;
    }

    //TODO: signal a barrier that we should start infer and timer. return threads and move stop outside?
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