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

#include "common/file_utils.hpp"
#include "common/latency_meter.hpp"

#include "network_runner.hpp"

#if defined(_MSC_VER)
#include <mmsystem.h>
#endif

using namespace hailort;

SignalEventScopeGuard::SignalEventScopeGuard(Event &event) :
    m_event(event)
{}

SignalEventScopeGuard::~SignalEventScopeGuard()
{
    m_event.signal();
}

BarrierTerminateScopeGuard::BarrierTerminateScopeGuard(BarrierPtr barrier) :
    m_barrier(barrier)
{}

BarrierTerminateScopeGuard::~BarrierTerminateScopeGuard()
{
    if (m_barrier) {
        m_barrier->terminate();
    }
}

#if defined(_MSC_VER) 
class TimeBeginScopeGuard final
{
public:
    TimeBeginScopeGuard() {
        // default interval between timer interrupts on Windows is 15.625 ms.
        // This will change it to be 1 ms, enabling us to sleep in granularity of 1 milliseconds.
        // As from Windows 10 2004, in general processes are no longer affected by other processes calling timeBeginPeriod.
        // https://randomascii.wordpress.com/2020/10/04/windows-timer-resolution-the-great-rule-change/
        timeBeginPeriod(1);
    }
    ~TimeBeginScopeGuard() {
        timeEndPeriod(1);
    }
};
#endif


//TODO: duplicated
hailo_status NetworkRunner::wait_for_threads(std::vector<AsyncThreadPtr<hailo_status>> &threads)
{
    auto last_error_status = HAILO_SUCCESS;
    for (auto &thread : threads) {
        auto thread_status = thread->get();
        if (!inference_succeeded(thread_status)) {
            last_error_status = thread_status;
            LOGGER__ERROR("Thread failed with with status {}", thread_status);
        }
    }
    return last_error_status;
}

IoParams::IoParams() : name(), input_file_path()
{
}

VStreamParams::VStreamParams() : IoParams(), params(HailoRTDefaults::get_vstreams_params())
{
}

StreamParams::StreamParams() : IoParams(), flags(HAILO_STREAM_FLAGS_NONE)
{
}

NetworkParams::NetworkParams() : hef_path(), net_group_name(), vstream_params(), stream_params(),
    scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN), batch_size(HAILO_DEFAULT_BATCH_SIZE),
    scheduler_threshold(0), scheduler_timeout_ms(0), framerate(UNLIMITED_FRAMERATE), measure_hw_latency(false),
    measure_overall_latency(false)
{
}

NetworkRunner::NetworkRunner(const NetworkParams &params, const std::string &name,
                             VDevice &vdevice, std::shared_ptr<ConfiguredNetworkGroup> cng) :
    m_vdevice(vdevice),
    m_params(params),
    m_name(name),
    m_cng(cng),
    m_overall_latency_meter(nullptr),
    m_latency_barrier(nullptr),
    m_last_measured_fps(0)
{
}

Expected<std::shared_ptr<NetworkRunner>> NetworkRunner::create_shared(VDevice &vdevice, const NetworkParams &params)
{
    // The network params passed to the NetworkRunner may be changed by this function, hence we copy them.
    auto final_net_params = params;

    auto hef = Hef::create(final_net_params.hef_path);
    CHECK_EXPECTED(hef);

    // Get NG's name if single
    auto net_group_name = final_net_params.net_group_name;
    if (net_group_name.empty()) {
        auto net_groups_names = hef->get_network_groups_names();
        CHECK_AS_EXPECTED(net_groups_names.size() == 1, HAILO_INVALID_ARGUMENT, "HEF {} doesn't contain a single NetworkGroup. Pass --name", final_net_params.hef_path);
        net_group_name = net_groups_names[0];
    }

    auto cfg_params = vdevice.create_configure_params(hef.value(), net_group_name);
    CHECK_EXPECTED(cfg_params);
    cfg_params->batch_size = final_net_params.batch_size;
    if (final_net_params.batch_size == HAILO_DEFAULT_BATCH_SIZE) {
        // Changing batch_size to 1. If HAILO_DEFAULT_BATCH_SIZE is configured, the sched will send one frame per batch
        final_net_params.batch_size = 1;
    }
    if (final_net_params.measure_hw_latency) {
        cfg_params->latency |= HAILO_LATENCY_MEASURE;
    }
    if (final_net_params.is_async()) {
        for (auto &stream_name_params_pair : cfg_params->stream_params_by_name) {
            stream_name_params_pair.second.flags = HAILO_STREAM_FLAGS_ASYNC;
        }
    } 
    auto cfgr_net_groups = vdevice.configure(hef.value(), {{net_group_name, cfg_params.value()}});
    CHECK_EXPECTED(cfgr_net_groups);
    assert(1 == cfgr_net_groups->size());
    auto cfgr_net_group = cfgr_net_groups.value()[0];

    if (HAILO_SCHEDULING_ALGORITHM_NONE!= final_net_params.scheduling_algorithm) {
        CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_threshold(final_net_params.scheduler_threshold));
        CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_timeout(std::chrono::milliseconds(final_net_params.scheduler_timeout_ms)));
        CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_priority(final_net_params.scheduler_priority));
    }

    std::shared_ptr<NetworkRunner> net_runner_ptr = nullptr;
    switch (final_net_params.mode)
    {
    case InferenceMode::FULL:
    {
        std::map<std::string, hailo_vstream_params_t> vstreams_params;
        for (auto &vstream_params : final_net_params.vstream_params) {
            vstreams_params.emplace(vstream_params.name, vstream_params.params);
        }
        auto vstreams = create_vstreams(*cfgr_net_group, vstreams_params);
        CHECK_EXPECTED(vstreams);

        auto net_runner = make_shared_nothrow<FullNetworkRunner>(final_net_params, net_group_name, vdevice,
            std::move(vstreams->first), std::move(vstreams->second), cfgr_net_group);
        CHECK_NOT_NULL_AS_EXPECTED(net_runner, HAILO_OUT_OF_HOST_MEMORY);
        net_runner_ptr = std::static_pointer_cast<NetworkRunner>(net_runner);
        break;
    }

    case InferenceMode::RAW:            // Fallthrough
    case InferenceMode::RAW_ASYNC:      // Fallthrough
    case InferenceMode::RAW_ASYNC_SINGLE_THREAD:
    {
        auto input_streams = cfgr_net_group->get_input_streams();
        CHECK_AS_EXPECTED(input_streams.size() > 0, HAILO_INTERNAL_FAILURE);

        auto output_streams = cfgr_net_group->get_output_streams();
        CHECK_AS_EXPECTED(output_streams.size() > 0, HAILO_INTERNAL_FAILURE);

        auto net_runner = make_shared_nothrow<RawNetworkRunner>(final_net_params, net_group_name, vdevice,
            std::move(input_streams), std::move(output_streams), cfgr_net_group);
        CHECK_NOT_NULL_AS_EXPECTED(net_runner, HAILO_OUT_OF_HOST_MEMORY);
        net_runner_ptr = std::static_pointer_cast<NetworkRunner>(net_runner);
        break;
    }

    default:
        // Shouldn't get here
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    if (final_net_params.measure_overall_latency || final_net_params.measure_hw_latency) {
        auto input_names = net_runner_ptr->get_input_names();
        auto output_names = net_runner_ptr->get_output_names();

        CHECK_AS_EXPECTED((1 == input_names.size()), HAILO_INVALID_OPERATION,
            "Latency measurement over multiple inputs network is not supported");

        if (final_net_params.measure_overall_latency) {
            auto overall_latency_meter = make_shared_nothrow<LatencyMeter>(output_names, OVERALL_LATENCY_TIMESTAMPS_LIST_LENGTH);
            CHECK_NOT_NULL_AS_EXPECTED(overall_latency_meter, HAILO_OUT_OF_HOST_MEMORY);
            net_runner_ptr->set_overall_latency_meter(overall_latency_meter);
        }

        // We use a barrier for both hw and overall latency
        auto latency_barrier = make_shared_nothrow<Barrier>(input_names.size() + output_names.size());
        CHECK_NOT_NULL_AS_EXPECTED(latency_barrier, HAILO_OUT_OF_HOST_MEMORY);
        net_runner_ptr->set_latency_barrier(latency_barrier);
    }

    return net_runner_ptr;
}

bool NetworkRunner::inference_succeeded(hailo_status status)
{
    const auto status_find_result = std::find(NetworkRunner::ALLOWED_INFERENCE_RETURN_VALUES.cbegin(),
        NetworkRunner::ALLOWED_INFERENCE_RETURN_VALUES.cend(), status);
    // If the status is in the allowed list, the inference has succeeded
    return status_find_result != NetworkRunner::ALLOWED_INFERENCE_RETURN_VALUES.cend();
}

hailo_status NetworkRunner::run(EventPtr shutdown_event, LiveStats &live_stats, Barrier &activation_barrier)
{
    auto ang = std::unique_ptr<ActivatedNetworkGroup>(nullptr);
    if (HAILO_SCHEDULING_ALGORITHM_NONE == m_params.scheduling_algorithm) {
        auto ang_exp = m_cng->activate();
        if (!ang_exp) {
            activation_barrier.terminate();
        }
        CHECK_EXPECTED_AS_STATUS(ang_exp);
        ang = ang_exp.release();
    }

    // If we measure latency (hw or overall) we send frames one at a time. Hence we don't measure fps.
    const auto measure_fps = !m_params.measure_hw_latency && !m_params.measure_overall_latency;
    auto net_live_track = std::make_shared<NetworkLiveTrack>(m_name, m_cng, m_overall_latency_meter, measure_fps, m_params.hef_path);
    live_stats.add(net_live_track, 1); //support progress over multiple outputs

#if defined(_MSC_VER)
    TimeBeginScopeGuard time_begin_scope_guard;
#endif

    activation_barrier.arrive_and_wait();

    if (m_params.mode == InferenceMode::RAW_ASYNC_SINGLE_THREAD) {
        return run_single_thread_async_infer(shutdown_event, net_live_track);
    } else {
        auto threads = start_inference_threads(shutdown_event, net_live_track);
        CHECK_EXPECTED_AS_STATUS(threads);

        CHECK_SUCCESS(shutdown_event->wait(HAILO_INFINITE_TIMEOUT));
        stop();
        return wait_for_threads(threads.value());
    }
}

void NetworkRunner::set_overall_latency_meter(LatencyMeterPtr latency_meter)
{
    m_overall_latency_meter = latency_meter;
}

void NetworkRunner::set_latency_barrier(BarrierPtr latency_barrier)
{
    m_latency_barrier = latency_barrier;
}

std::shared_ptr<ConfiguredNetworkGroup> NetworkRunner::get_configured_network_group()
{
    return m_cng;
}

void NetworkRunner::set_last_measured_fps(double fps)
{
    m_last_measured_fps = fps;
}

double NetworkRunner::get_last_measured_fps()
{
    return m_last_measured_fps;
}

hailo_vstream_params_t update_quantize_flag_in_vstream_param(const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &old_vstream_params)
{
    hailo_vstream_params_t res = old_vstream_params;
    if ((HAILO_FORMAT_TYPE_FLOAT32 == old_vstream_params.user_buffer_format.type) || (HailoRTCommon::is_nms(vstream_info))) {
        res.user_buffer_format.flags &= (~HAILO_FORMAT_FLAGS_QUANTIZED);
    } else {
        res.user_buffer_format.flags |= (HAILO_FORMAT_FLAGS_QUANTIZED);
    }
    return res;
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
            auto vstream_param = update_quantize_flag_in_vstream_param(input_vstream_info, elem_it->second);
            input_vstreams_params.emplace(input_vstream_info.name, vstream_param);
            match_count++;
        } else {
            auto vstream_param = update_quantize_flag_in_vstream_param(input_vstream_info, HailoRTDefaults::get_vstreams_params());
            input_vstreams_params.emplace(input_vstream_info.name, vstream_param);
        }
    }

    std::map<std::string, hailo_vstream_params_t> output_vstreams_params;
    auto output_vstreams_info = net_group.get_output_vstream_infos();
    CHECK_EXPECTED(output_vstreams_info);
    for (auto &output_vstream_info : output_vstreams_info.value()) {
        auto elem_it = params.find(output_vstream_info.name);
        if (elem_it != params.end()) {
            auto vstream_param = update_quantize_flag_in_vstream_param(output_vstream_info, elem_it->second);
            output_vstreams_params.emplace(output_vstream_info.name, vstream_param);
            match_count++;
        }
        else {
            auto vstream_param = update_quantize_flag_in_vstream_param(output_vstream_info, HailoRTDefaults::get_vstreams_params());
            output_vstreams_params.emplace(output_vstream_info.name, vstream_param);
        }
    }

    CHECK(match_count == params.size(), make_unexpected(HAILO_INVALID_ARGUMENT), "One of the params has an invalid vStream name");

    auto input_vstreams = VStreamsBuilder::create_input_vstreams(net_group, input_vstreams_params);
    CHECK_EXPECTED(input_vstreams);

    auto output_vstreams = VStreamsBuilder::create_output_vstreams(net_group, output_vstreams_params);
    CHECK_EXPECTED(output_vstreams);

    return {{input_vstreams.release(), output_vstreams.release()}};//TODO: move? copy elision?
}

const std::vector<hailo_status> NetworkRunner::ALLOWED_INFERENCE_RETURN_VALUES{
    {HAILO_SUCCESS, HAILO_STREAM_ABORTED_BY_USER, HAILO_SHUTDOWN_EVENT_SIGNALED}
};

FullNetworkRunner::FullNetworkRunner(const NetworkParams &params, const std::string &name, VDevice &vdevice,
                                     std::vector<InputVStream> &&input_vstreams, std::vector<OutputVStream> &&output_vstreams,
                                     std::shared_ptr<ConfiguredNetworkGroup> cng) :
    NetworkRunner(params, name, vdevice, cng),
    m_input_vstreams(std::move(input_vstreams)),
    m_output_vstreams(std::move(output_vstreams))
{
}

Expected<std::vector<AsyncThreadPtr<hailo_status>>> FullNetworkRunner::start_inference_threads(EventPtr shutdown_event,
    std::shared_ptr<NetworkLiveTrack> net_live_track)
{
    std::vector<AsyncThreadPtr<hailo_status>> threads;
    for (auto &input_vstream : m_input_vstreams) {
        const auto vstream_params = get_params(input_vstream.name());
        auto writer = WriterWrapper<InputVStream>::create(input_vstream, vstream_params, m_overall_latency_meter,
            m_params.framerate);
        CHECK_EXPECTED(writer);

        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("WRITE",
            [this, writer = writer.release(), shutdown_event]() mutable {
                return run_write(writer, shutdown_event, m_latency_barrier);
            }));
    }

    bool first = true; //TODO: check with multiple outputs
    for (auto &output_vstream : m_output_vstreams) {
        auto reader = ReaderWrapper<OutputVStream>::create(output_vstream, m_overall_latency_meter,
            first ? net_live_track : nullptr);
        CHECK_EXPECTED(reader);

        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("READ",
            [this, reader=reader.release(), shutdown_event]() mutable {
                return run_read(reader, shutdown_event, m_latency_barrier);
            }));
        first = false;
    }

    return threads;
}

void FullNetworkRunner::stop()
{
    for (auto &input_vstream : m_input_vstreams) {
        (void) input_vstream.abort();
    }
    for (auto &output_vstream : m_output_vstreams) {
        (void) output_vstream.abort();
    }
}

std::set<std::string> FullNetworkRunner::get_input_names()
{
    std::set<std::string> result;

    for (const auto &vstream : m_input_vstreams) {
        result.insert(vstream.name());
    }

    return result;
}

std::set<std::string> FullNetworkRunner::get_output_names()
{
    std::set<std::string> result;

    for (const auto &vstream : m_output_vstreams) {
        result.insert(vstream.name());
    }

    return result;
}

VStreamParams FullNetworkRunner::get_params(const std::string &name)
{
    for (const auto &params : m_params.vstream_params) {
        if (name == params.name) {
            return params;
        }
    }
    return VStreamParams();
}

RawNetworkRunner::RawNetworkRunner(const NetworkParams &params, const std::string &name, VDevice &vdevice,
                                   InputStreamRefVector &&input_streams, OutputStreamRefVector &&output_streams,
                                   std::shared_ptr<ConfiguredNetworkGroup> cng) :
    NetworkRunner(params, name, vdevice, cng),
    m_input_streams(std::move(input_streams)),
    m_output_streams(std::move(output_streams))
{
}

Expected<std::vector<AsyncThreadPtr<hailo_status>>> RawNetworkRunner::start_inference_threads(EventPtr shutdown_event,
    std::shared_ptr<NetworkLiveTrack> net_live_track)
{
    const bool async_streams = (m_params.is_async());
    std::vector<AsyncThreadPtr<hailo_status>> threads;
    for (auto &input_stream : m_input_streams) {
        const auto stream_params = get_params(input_stream.get().name());
        auto writer = WriterWrapper<InputStream>::create(input_stream.get(), stream_params, m_overall_latency_meter,
            m_params.framerate);
        CHECK_EXPECTED(writer);

        if (async_streams) {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("WRITE_ASYNC",
                [this, writer = writer.release(), shutdown_event]() mutable {
                    return run_write_async(writer, shutdown_event, m_latency_barrier);
                }));
        } else {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("WRITE",
                [this, writer = writer.release(), shutdown_event]() mutable {
                    return run_write(writer, shutdown_event, m_latency_barrier);
                }));
        }
    }

    bool first = true; //TODO: check with multiple outputs
    for (auto &output_stream : m_output_streams) {
        auto reader = ReaderWrapper<OutputStream>::create(output_stream.get(), m_overall_latency_meter,
            first ? net_live_track : nullptr);
        CHECK_EXPECTED(reader);

        if (async_streams) {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("READ_ASYNC",
                [this, reader=reader.release(), shutdown_event]() mutable {
                    return run_read_async(reader, shutdown_event, m_latency_barrier);
                }));
        } else {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("READ",
                [this, reader=reader.release(), shutdown_event]() mutable {
                    return run_read(reader, shutdown_event, m_latency_barrier);
                }));
        }
        first = false;
    }

    return threads;
}

hailo_status RawNetworkRunner::run_single_thread_async_infer(EventPtr shutdown_event,
    std::shared_ptr<NetworkLiveTrack> net_live_track)
{
    // Build output wrappers
    std::vector<ReaderWrapperPtr<OutputStream>> reader_wrappers;
    std::vector<SemaphorePtr> output_semaphores;
    bool is_first_output = true;
    for (auto &output_stream : m_output_streams) {
        auto reader_wrapper = ReaderWrapper<OutputStream>::create(output_stream.get(), m_overall_latency_meter,
            is_first_output ? net_live_track : nullptr);
        CHECK_EXPECTED_AS_STATUS(reader_wrapper);
        is_first_output = false;

        auto max_queue_size = reader_wrapper.value()->get().get_async_max_queue_size();
        CHECK_EXPECTED_AS_STATUS(max_queue_size);

        auto semaphore = Semaphore::create_shared(static_cast<uint32_t>(*max_queue_size));
        CHECK_NOT_NULL(semaphore, HAILO_OUT_OF_HOST_MEMORY);

        output_semaphores.emplace_back(semaphore);
        reader_wrappers.emplace_back(reader_wrapper.release());
    }

    // Build input wrappers
    std::vector<WriterWrapperPtr<InputStream>> writer_wrappers;
    std::vector<SemaphorePtr> input_semaphores;
    for (auto &input_stream : m_input_streams) {
        auto writer_wrapper = WriterWrapper<InputStream>::create(input_stream.get(),
            get_params(input_stream.get().name()), m_overall_latency_meter, m_params.framerate);
        CHECK_EXPECTED_AS_STATUS(writer_wrapper);

        auto max_queue_size = writer_wrapper.value()->get().get_async_max_queue_size();
        CHECK_EXPECTED_AS_STATUS(max_queue_size);

        auto semaphore = Semaphore::create_shared(static_cast<uint32_t>(*max_queue_size));
        CHECK_NOT_NULL(semaphore, HAILO_OUT_OF_HOST_MEMORY);

        input_semaphores.emplace_back(semaphore);
        writer_wrappers.emplace_back(writer_wrapper.release());
    }

    // Build waitables list with reference to previous input/output semaphores.
    // We put output semaphores before inputs because we want to always have place to write
    // the data into. It also makes sure that the framerate throttle will work properly.
    const size_t shutdown_index = 0;
    const size_t output_index_start = shutdown_index + 1;
    const size_t input_index_start = output_index_start + output_semaphores.size();

    std::vector<std::reference_wrapper<Waitable>> waitables;
    waitables.emplace_back(std::ref(*shutdown_event));
    auto add_to_waitables = [&waitables](const SemaphorePtr &sem) { waitables.emplace_back(std::ref(*sem)); };
    std::for_each(output_semaphores.begin(), output_semaphores.end(), add_to_waitables);
    std::for_each(input_semaphores.begin(), input_semaphores.end(), add_to_waitables);
    WaitableGroup wait_group(std::move(waitables));

    // Inference
    while (true) {
        auto wait_index = wait_group.wait_any(HAILORTCLI_DEFAULT_TIMEOUT);
        CHECK_EXPECTED_AS_STATUS(wait_index);

        if (*wait_index == shutdown_index) {
            // Stopping the network so we won't get timeout on the flush. The async operations may still be active
            // (until network deactivation).
            stop();
            break;
        } else if ((*wait_index >= output_index_start) && (*wait_index < input_index_start)) {
            // output is ready
            const size_t output_index = *wait_index - output_index_start;
            auto status = reader_wrappers[output_index]->read_async(
                [semaphore=output_semaphores[output_index]](const OutputStream::CompletionInfo &) {
                    (void)semaphore->signal();
                }
            );
            CHECK_SUCCESS(status);
        } else {
            // input is ready
            const size_t input_index = *wait_index - input_index_start;
            auto status = writer_wrappers[input_index]->write_async(
                [semaphore=input_semaphores[input_index]](const InputStream::CompletionInfo &) {
                    (void)semaphore->signal();
                }
            );
            CHECK_SUCCESS(status);
        }
    }

    return HAILO_SUCCESS;
}

void RawNetworkRunner::stop()
{
    for (auto &input_stream : m_input_streams) {
        (void) input_stream.get().abort();
    }
    for (auto &output_stream : m_output_streams) {
        (void) output_stream.get().abort();
    }
}

std::set<std::string> RawNetworkRunner::get_input_names()
{
    std::set<std::string> result;
    for (const auto &stream : m_input_streams) {
        result.insert(stream.get().name());
    }

    return result;
}

std::set<std::string> RawNetworkRunner::get_output_names()
{
    std::set<std::string> result;
    for (const auto &stream : m_output_streams) {
        result.insert(stream.get().name());
    }

    return result;
}

StreamParams RawNetworkRunner::get_params(const std::string &name)
{
    for (const auto &params : m_params.stream_params) {
        if (name == params.name) {
            return params;
        }
    }
    return StreamParams();
}