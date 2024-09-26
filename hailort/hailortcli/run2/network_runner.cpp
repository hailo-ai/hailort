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
    scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN), multi_process_service(false),
    batch_size(HAILO_DEFAULT_BATCH_SIZE), scheduler_threshold(0), scheduler_timeout_ms(0),
    framerate(UNLIMITED_FRAMERATE), measure_hw_latency(false),measure_overall_latency(false)
{
}

NetworkRunner::NetworkRunner(const NetworkParams &params, const std::string &name,
                             VDevice &vdevice, std::shared_ptr<ConfiguredNetworkGroup> cng) :
    m_vdevice(vdevice),
    m_params(params),
    m_name(name),
    m_cng(cng),
    m_infer_model(nullptr),
    m_configured_infer_model(nullptr),
    m_overall_latency_meter(nullptr),
    m_latency_barrier(nullptr),
    m_last_measured_fps(0)
{
}

NetworkRunner::NetworkRunner(const NetworkParams &params, const std::string &name, VDevice &vdevice,
    std::shared_ptr<InferModel> infer_model, std::shared_ptr<ConfiguredInferModel> configured_infer_model) :
    m_vdevice(vdevice),
    m_params(params),
    m_name(name),
    m_cng(nullptr),
    m_infer_model(infer_model),
    m_configured_infer_model(configured_infer_model),
    m_overall_latency_meter(nullptr),
    m_latency_barrier(nullptr)
{
}

Expected<std::string> NetworkRunner::get_network_group_name(const NetworkParams &params, const Hef &hef)
{
    // Get NG's name if single
    auto net_group_name = params.net_group_name;

    // if net_group_name is an empty string - take the name from hef
    if (net_group_name.empty()) {
        auto net_groups_names = hef.get_network_groups_names();
        CHECK_AS_EXPECTED(net_groups_names.size() == 1, HAILO_INVALID_ARGUMENT, "HEF {} doesn't contain a single NetworkGroup. Pass --name", params.hef_path);
        net_group_name = net_groups_names[0];
    }

    return net_group_name;
}

Expected<std::shared_ptr<FullAsyncNetworkRunner>> FullAsyncNetworkRunner::create_shared(VDevice &vdevice,
    NetworkParams params)
{
    std::string net_group_name = params.net_group_name;
    if (net_group_name.empty()) {
        TRY(auto hef, Hef::create(params.hef_path));
        TRY(net_group_name, get_network_group_name(params, hef));
    }
    TRY(auto infer_model_ptr, vdevice.create_infer_model(params.hef_path, net_group_name));

    /* Validate params */
    for (const auto &vstream_params : params.vstream_params) {
        CHECK_AS_EXPECTED((contains(infer_model_ptr->get_input_names(), vstream_params.name)) ||
            (contains(infer_model_ptr->get_output_names(), vstream_params.name)),
            HAILO_INVALID_ARGUMENT, "The model doesnt have an edge with the given name '{}'", vstream_params.name);
    }

    /* Configure Params */
    infer_model_ptr->set_batch_size(params.batch_size);
    if (params.batch_size == HAILO_DEFAULT_BATCH_SIZE) {
        // Changing batch_size to 1 (after configuring the vdevice) - as we iterate over 'params.batch_size' in latency measurements scenarios
        params.batch_size = 1;
    }
    if (params.measure_hw_latency) {
        infer_model_ptr->set_hw_latency_measurement_flags(HAILO_LATENCY_MEASURE);
    }

    /* Pipeline Params */
    for (const auto &input_name : infer_model_ptr->get_input_names()) {
        auto input_params_it = std::find_if(params.vstream_params.begin(), params.vstream_params.end(),
            [&input_name](const VStreamParams &params) -> bool {
                return params.name == input_name;
            });
        auto input_params = (input_params_it == params.vstream_params.end()) ? VStreamParams() : *input_params_it;

        TRY(auto input_config, infer_model_ptr->input(input_name));
        input_config.set_format_order(input_params.params.user_buffer_format.order);
        input_config.set_format_type(input_params.params.user_buffer_format.type);
    }
    for (const auto &output_name : infer_model_ptr->get_output_names()) {
        auto output_params_it = std::find_if(params.vstream_params.begin(), params.vstream_params.end(),
            [&output_name](const VStreamParams &params) -> bool {
                return params.name == output_name;
            });
        auto output_params = (output_params_it == params.vstream_params.end()) ? VStreamParams() : *output_params_it;

        TRY(auto output_config, infer_model_ptr->output(output_name));
        output_config.set_format_order(output_params.params.user_buffer_format.order);
        output_config.set_format_type(output_params.params.user_buffer_format.type);
    }

    TRY(auto configured_model, infer_model_ptr->configure());
    auto configured_infer_model_ptr = make_shared_nothrow<ConfiguredInferModel>(std::move(configured_model));
    CHECK_NOT_NULL_AS_EXPECTED(configured_infer_model_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto res = make_shared_nothrow<FullAsyncNetworkRunner>(params, net_group_name, vdevice,
        infer_model_ptr, configured_infer_model_ptr);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    if (params.measure_overall_latency || params.measure_hw_latency) {
        CHECK_AS_EXPECTED((1 == res->get_input_names().size()), HAILO_INVALID_OPERATION,
            "Latency measurement over multiple inputs network is not supported");

        if (params.measure_overall_latency) {
            auto overall_latency_meter = make_shared_nothrow<LatencyMeter>(std::set<std::string>{ "INFERENCE" }, // Since we check 'infer()' with single callback, we only address 1 output
                OVERALL_LATENCY_TIMESTAMPS_LIST_LENGTH);
            CHECK_NOT_NULL_AS_EXPECTED(overall_latency_meter, HAILO_OUT_OF_HOST_MEMORY);
            res->set_overall_latency_meter(overall_latency_meter);
        }

        // We use a barrier for both hw and overall latency
        auto latency_barrier = make_shared_nothrow<Barrier>(1); // Only 1 frame at a time
        CHECK_NOT_NULL_AS_EXPECTED(latency_barrier, HAILO_OUT_OF_HOST_MEMORY);
        res->set_latency_barrier(latency_barrier);
    }
    return res;
}

Expected<std::shared_ptr<NetworkRunner>> NetworkRunner::create_shared(VDevice &vdevice, const NetworkParams &params)
{
    // The network params passed to the NetworkRunner may be changed by this function, hence we copy them.
    auto final_net_params = params;

    std::shared_ptr<NetworkRunner> net_runner_ptr = nullptr;
    if (InferenceMode::FULL_ASYNC == final_net_params.mode) {
        TRY(net_runner_ptr, FullAsyncNetworkRunner::create_shared(vdevice, final_net_params));
    } else {
        TRY(auto hef, Hef::create(final_net_params.hef_path));
        TRY(auto net_group_name, get_network_group_name(final_net_params, hef));
        TRY(auto cfg_params, vdevice.create_configure_params(hef, net_group_name));
        cfg_params.batch_size = final_net_params.batch_size;
        if (final_net_params.batch_size == HAILO_DEFAULT_BATCH_SIZE) {
            // Changing batch_size to 1 (after configuring the vdevice) - as we iterate over 'final_net_params.batch_size' in latency measurements scenarios
            final_net_params.batch_size = 1;
        }
        if (final_net_params.measure_hw_latency) {
            cfg_params.latency |= HAILO_LATENCY_MEASURE;
        }
        if (final_net_params.is_async()) {
            for (auto &stream_name_params_pair : cfg_params.stream_params_by_name) {
                stream_name_params_pair.second.flags = HAILO_STREAM_FLAGS_ASYNC;
            }
        }
        TRY(auto cfgr_net_groups, vdevice.configure(hef, {{ net_group_name, cfg_params }}));
        assert(1 == cfgr_net_groups.size());
        auto cfgr_net_group = cfgr_net_groups[0];

        if (HAILO_SCHEDULING_ALGORITHM_NONE != final_net_params.scheduling_algorithm) {
            CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_threshold(final_net_params.scheduler_threshold));
            CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_timeout(std::chrono::milliseconds(final_net_params.scheduler_timeout_ms)));
            CHECK_SUCCESS_AS_EXPECTED(cfgr_net_group->set_scheduler_priority(final_net_params.scheduler_priority));
        }

        switch (final_net_params.mode)
        {
        case InferenceMode::FULL_SYNC:
        {
            std::map<std::string, hailo_vstream_params_t> vstreams_params;
            for (auto &vstream_params : final_net_params.vstream_params) {
                vstreams_params.emplace(vstream_params.name, vstream_params.params);
            }
            TRY(auto vstreams, create_vstreams(*cfgr_net_group, vstreams_params));

            auto net_runner = make_shared_nothrow<FullSyncNetworkRunner>(final_net_params, net_group_name, vdevice,
                std::move(vstreams.first), std::move(vstreams.second), cfgr_net_group);
            CHECK_NOT_NULL_AS_EXPECTED(net_runner, HAILO_OUT_OF_HOST_MEMORY);
            net_runner_ptr = std::static_pointer_cast<NetworkRunner>(net_runner);
            break;
        }
        case InferenceMode::RAW_SYNC:       // Fallthrough
        case InferenceMode::RAW_ASYNC:      // Fallthrough
        case InferenceMode::RAW_ASYNC_SINGLE_THREAD:
        {
            auto input_streams = cfgr_net_group->get_input_streams();
            CHECK_AS_EXPECTED(input_streams.size() > 0, HAILO_INTERNAL_FAILURE);

            auto output_streams = cfgr_net_group->get_output_streams();
            CHECK_AS_EXPECTED(output_streams.size() > 0, HAILO_INTERNAL_FAILURE);

            /* Validate params */
            for (const auto &stream_param : final_net_params.stream_params) {
                CHECK_AS_EXPECTED(
                    (std::any_of(input_streams.begin(), input_streams.end(), [name = stream_param.name] (const auto &stream) { return name == stream.get().name(); })) ||
                        (std::any_of(output_streams.begin(), output_streams.end(), [name = stream_param.name] (const auto &stream) { return name == stream.get().name(); })),
                    HAILO_INVALID_ARGUMENT, "The model doesnt have an edge with the given name '{}'", stream_param.name);
            }

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
        if (m_cng) {
            auto ang_exp = m_cng->activate();
            if (!ang_exp) {
                activation_barrier.terminate();
            }
            CHECK_EXPECTED_AS_STATUS(ang_exp); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
            ang = ang_exp.release();
        }
    }

    // If we measure latency (hw or overall) we send frames one at a time. Hence we don't measure fps.
    const auto measure_fps = !m_params.measure_hw_latency && !m_params.measure_overall_latency;
    auto net_live_track = std::make_shared<NetworkLiveTrack>(m_name, m_cng, m_configured_infer_model, m_overall_latency_meter, measure_fps, m_params.hef_path);
    live_stats.add(net_live_track, 1); //support progress over multiple outputs

#if defined(_MSC_VER)
    TimeBeginScopeGuard time_begin_scope_guard;
#endif

    activation_barrier.arrive_and_wait();

    if ((InferenceMode::RAW_ASYNC_SINGLE_THREAD == m_params.mode) || (InferenceMode::FULL_ASYNC == m_params.mode)) {
        return run_single_thread_async_infer(shutdown_event, net_live_track);
    } else {
        TRY(auto threads, start_inference_threads(shutdown_event, net_live_track));

        CHECK_SUCCESS(shutdown_event->wait(HAILO_INFINITE_TIMEOUT));
        stop();
        return wait_for_threads(threads);
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

Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> NetworkRunner::create_vstreams(
    ConfiguredNetworkGroup &net_group, const std::map<std::string, hailo_vstream_params_t> &params)
{//TODO: support network name

    /* Validate params */
    TRY(auto input_vstreams_info, net_group.get_input_vstream_infos());
    TRY(auto output_vstreams_info, net_group.get_output_vstream_infos());
    for (const auto &pair : params) {
        CHECK_AS_EXPECTED(
            (std::any_of(input_vstreams_info.begin(), input_vstreams_info.end(), [name = pair.first] (const auto &info) { return name == std::string(info.name); })) ||
                (std::any_of(output_vstreams_info.begin(), output_vstreams_info.end(), [name = pair.first] (const auto &info) { return name == std::string(info.name); })),
            HAILO_INVALID_ARGUMENT, "The model doesnt have an edge with the given name '{}'", pair.first);
    }

    std::map<std::string, hailo_vstream_params_t> input_vstreams_params;
    for (auto &input_vstream_info : input_vstreams_info) {
        if (params.end() != params.find(input_vstream_info.name)) {
            input_vstreams_params.emplace(input_vstream_info.name, params.at(input_vstream_info.name));
        } else {
            input_vstreams_params.emplace(input_vstream_info.name, HailoRTDefaults::get_vstreams_params());
        }
    }

    std::map<std::string, hailo_vstream_params_t> output_vstreams_params;
    for (auto &output_vstream_info : output_vstreams_info) {
        if (params.end() != params.find(output_vstream_info.name)) {
            output_vstreams_params.emplace(output_vstream_info.name, params.at(output_vstream_info.name));
        } else {
            output_vstreams_params.emplace(output_vstream_info.name, HailoRTDefaults::get_vstreams_params());
        }
    }

    TRY(auto input_vstreams, VStreamsBuilder::create_input_vstreams(net_group, input_vstreams_params));
    TRY(auto output_vstreams, VStreamsBuilder::create_output_vstreams(net_group, output_vstreams_params));

    return std::make_pair(std::move(input_vstreams), std::move(output_vstreams));
}

const std::vector<hailo_status> NetworkRunner::ALLOWED_INFERENCE_RETURN_VALUES{
    {HAILO_SUCCESS, HAILO_STREAM_ABORT, HAILO_SHUTDOWN_EVENT_SIGNALED}
};

FullSyncNetworkRunner::FullSyncNetworkRunner(const NetworkParams &params, const std::string &name, VDevice &vdevice,
                                     std::vector<InputVStream> &&input_vstreams, std::vector<OutputVStream> &&output_vstreams,
                                     std::shared_ptr<ConfiguredNetworkGroup> cng) :
    NetworkRunner(params, name, vdevice, cng),
    m_input_vstreams(std::move(input_vstreams)),
    m_output_vstreams(std::move(output_vstreams))
{
}

Expected<std::vector<AsyncThreadPtr<hailo_status>>> FullSyncNetworkRunner::start_inference_threads(EventPtr shutdown_event,
    std::shared_ptr<NetworkLiveTrack> net_live_track)
{
    static const bool SYNC_API = false;
    std::vector<AsyncThreadPtr<hailo_status>> threads;
    for (auto &input_vstream : m_input_vstreams) {
        const auto vstream_params = get_params(input_vstream.name());
        TRY(auto writer, WriterWrapper<InputVStream>::create(input_vstream, vstream_params, m_vdevice,
            m_overall_latency_meter, m_params.framerate, SYNC_API));

        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("WRITE",
            [this, writer, shutdown_event]() mutable {
                return run_write(writer, shutdown_event, m_latency_barrier);
            }));
    }

    bool first = true; //TODO: check with multiple outputs
    for (auto &output_vstream : m_output_vstreams) {
        TRY(auto reader, ReaderWrapper<OutputVStream>::create(output_vstream, m_vdevice,
            m_overall_latency_meter, first ? net_live_track : nullptr, SYNC_API));

        threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("READ",
            [this, reader, shutdown_event]() mutable {
                return run_read(reader, shutdown_event, m_latency_barrier);
            }));
        first = false;
    }

    return threads;
}

void FullSyncNetworkRunner::stop()
{
    (void) m_cng->shutdown();
}

std::set<std::string> FullSyncNetworkRunner::get_input_names()
{
    std::set<std::string> result;

    for (const auto &vstream : m_input_vstreams) {
        result.insert(vstream.name());
    }

    return result;
}

std::set<std::string> FullSyncNetworkRunner::get_output_names()
{
    std::set<std::string> result;

    for (const auto &vstream : m_output_vstreams) {
        result.insert(vstream.name());
    }

    return result;
}

VStreamParams FullSyncNetworkRunner::get_params(const std::string &name)
{
    for (const auto &params : m_params.vstream_params) {
        if (name == params.name) {
            return params;
        }
    }
    return VStreamParams();
}


FullAsyncNetworkRunner::FullAsyncNetworkRunner(const NetworkParams &params, const std::string &name, VDevice &vdevice,
    std::shared_ptr<InferModel> infer_model, std::shared_ptr<ConfiguredInferModel> configured_infer_model) :
    NetworkRunner(params, name, vdevice, infer_model, configured_infer_model)
{
}

void FullAsyncNetworkRunner::stop()
{}

std::set<std::string> FullAsyncNetworkRunner::get_input_names()
{
    std::set<std::string> results;
    for (const auto &name : m_infer_model->get_input_names()) {
        results.insert(name);
    }
    return results;
}

std::set<std::string> FullAsyncNetworkRunner::get_output_names()
{
    std::set<std::string> results;
    for (const auto &name : m_infer_model->get_output_names()) {
        results.insert(name);
    }
    return results;
}

VStreamParams FullAsyncNetworkRunner::get_params(const std::string &name)
{
    for (const auto &params : m_params.vstream_params) {
        if (name == params.name) {
            return params;
        }
    }
    return VStreamParams();
}

Expected<AsyncInferJob> FullAsyncNetworkRunner::create_infer_job(const ConfiguredInferModel::Bindings &bindings,
    std::weak_ptr<NetworkLiveTrack> net_live_track_weak, FramerateThrottle &frame_rate_throttle, hailo_status &inference_status)
{
    frame_rate_throttle.throttle();
    if (m_overall_latency_meter) {
        m_overall_latency_meter->add_start_sample(std::chrono::steady_clock::now().time_since_epoch());
    }

    TRY(auto job, m_configured_infer_model->run_async(bindings, [=, &inference_status] (const AsyncInferCompletionInfo &completion_info) {
        if (HAILO_SUCCESS != completion_info.status) {
            inference_status = completion_info.status;
            if (HAILO_STREAM_ABORT != completion_info.status) {
                LOGGER__ERROR("Failed in infer async request");
            }
            return;
        }
        if (m_overall_latency_meter) {
            m_overall_latency_meter->add_end_sample("INFERENCE", std::chrono::steady_clock::now().time_since_epoch());
        }
        if (auto net_live_track = net_live_track_weak.lock()) {
            /* Using weak_ptr as net_live_track holds a reference to m_configured_infer_model (for stuff like latency measurement),
                so there's a circular dependency */
            net_live_track->progress();
        }
    }));
    return job;
}

hailo_status FullAsyncNetworkRunner::run_single_thread_async_infer(EventPtr shutdown_event,
    std::shared_ptr<NetworkLiveTrack> net_live_track)
{
    auto signal_event_scope_guard = SignalEventScopeGuard(*shutdown_event);

    std::unique_ptr<ConfiguredInferModelActivationGuard> guard = nullptr;
    if (HAILO_SCHEDULING_ALGORITHM_NONE != m_params.scheduling_algorithm) {
        auto status = m_configured_infer_model->set_scheduler_threshold(m_params.scheduler_threshold);
        CHECK_SUCCESS(status);

        status = m_configured_infer_model->set_scheduler_timeout(std::chrono::milliseconds(m_params.scheduler_timeout_ms));
        CHECK_SUCCESS(status);

        status = m_configured_infer_model->set_scheduler_priority(m_params.scheduler_priority);
        CHECK_SUCCESS(status);
    } else {
        TRY(guard, ConfiguredInferModelActivationGuard::create(m_configured_infer_model));
    }

    TRY(auto bindings, m_configured_infer_model->create_bindings());

    std::unordered_map<std::string, Buffer> input_buffers; // Keys are inputs names
    std::vector<Buffer> output_buffers;
    std::vector<DmaMappedBuffer> dma_mapped_buffers;

    const uint8_t const_byte = 0xAB;
    for (const auto &name : get_input_names()) {
        TRY(auto input_config, m_infer_model->input(name));

        auto params = get_params(name);
        Buffer buffer {};
        if (params.input_file_path.empty()) {
            TRY(buffer, Buffer::create(input_config.get_frame_size(), const_byte, BufferStorageParams::create_dma()));
        } else {
            TRY(buffer, read_binary_file(params.input_file_path, BufferStorageParams::create_dma()));
        }
        CHECK(0 == (buffer.size() % input_config.get_frame_size()), HAILO_INVALID_ARGUMENT,
            "Size of data for input '{}' must be a multiple of the frame size {}. Received - {}", name, input_config.get_frame_size(), buffer.size());
        input_buffers.emplace(name, std::move(buffer));

        for (uint32_t i = 0; i < (input_buffers.at(name).size() % input_config.get_frame_size()); i++) {
            TRY(auto mapped_buffer, DmaMappedBuffer::create(m_vdevice, input_buffers.at(name).data() + (i * input_config.get_frame_size()),
                input_config.get_frame_size(), HAILO_DMA_BUFFER_DIRECTION_H2D));
            dma_mapped_buffers.emplace_back(std::move(mapped_buffer));
        }
    }

    for (const auto &name : get_output_names()) {
        TRY(auto output_config, m_infer_model->output(name));
        TRY(auto buffer, Buffer::create(output_config.get_frame_size(), 0, BufferStorageParams::create_dma()));
        output_buffers.emplace_back(std::move(buffer));

        TRY(auto mapped_buffer, DmaMappedBuffer::create(m_vdevice, output_buffers.back().data(), output_buffers.back().size(),
            HAILO_DMA_BUFFER_DIRECTION_D2H));
        dma_mapped_buffers.emplace_back(std::move(mapped_buffer));

        CHECK_SUCCESS(bindings.output(name)->set_buffer(MemoryView(output_buffers.back())));
    }

    FramerateThrottle frame_rate_throttle(m_params.framerate);

    AsyncInferJob last_job;
    auto inference_status = HAILO_SUCCESS;
    uint32_t frame_id = 0;
    while (HAILO_TIMEOUT == shutdown_event->wait(std::chrono::milliseconds(0)) && (HAILO_SUCCESS == inference_status)) {
        for (uint32_t frames_in_cycle = 0; frames_in_cycle < m_params.batch_size; frames_in_cycle++) {
            for (const auto &name : get_input_names()) {
                TRY(auto input_config, m_infer_model->input(name));
                auto offset = (frame_id % (input_buffers.at(name).size() / input_config.get_frame_size())) * input_config.get_frame_size();
                CHECK_SUCCESS(bindings.input(name)->set_buffer(MemoryView(input_buffers.at(name).data() + offset,
                    input_config.get_frame_size())));
            }
            frame_id++;
            if (HAILO_SUCCESS == m_configured_infer_model->wait_for_async_ready(DEFAULT_TRANSFER_TIMEOUT)) {
                TRY(last_job, create_infer_job(bindings, net_live_track, frame_rate_throttle, inference_status));
                last_job.detach();
            }
        }
        if (m_latency_barrier) {
            // When measuring latency we want to send 'batch' frames at a time
            last_job.wait(HAILO_INFINITE_TIMEOUT);
        }
    }
    m_configured_infer_model->shutdown();
    last_job.wait(HAILO_INFINITE_TIMEOUT);

    return inference_status;
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
        TRY(auto writer, WriterWrapper<InputStream>::create(input_stream.get(), stream_params, m_vdevice,
            m_overall_latency_meter, m_params.framerate, async_streams));

        if (async_streams) {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("WRITE_ASYNC",
                [this, writer, shutdown_event]() mutable {
                    return run_write_async(writer, shutdown_event, m_latency_barrier);
                }));
        } else {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("WRITE",
                [this, writer, shutdown_event]() mutable {
                    return run_write(writer, shutdown_event, m_latency_barrier);
                }));
        }
    }

    bool first = true; //TODO: check with multiple outputs
    for (auto &output_stream : m_output_streams) {
        TRY(auto reader, ReaderWrapper<OutputStream>::create(output_stream.get(), m_vdevice,
            m_overall_latency_meter, first ? net_live_track : nullptr, async_streams));

        if (async_streams) {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("READ_ASYNC",
                [this, reader, shutdown_event]() mutable {
                    return run_read_async(reader, shutdown_event, m_latency_barrier);
                }));
        } else {
            threads.emplace_back(std::make_unique<AsyncThread<hailo_status>>("READ",
                [this, reader, shutdown_event]() mutable {
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
    static const bool ASYNC_API = true;

    // Build output wrappers
    std::vector<ReaderWrapperPtr<OutputStream>> reader_wrappers;
    std::vector<SemaphorePtr> output_semaphores;
    bool is_first_output = true;
    for (auto &output_stream : m_output_streams) {
        TRY(auto reader_wrapper, ReaderWrapper<OutputStream>::create(output_stream.get(), m_vdevice,
            m_overall_latency_meter, is_first_output ? net_live_track : nullptr, ASYNC_API));
        is_first_output = false;

        TRY(auto max_queue_size, reader_wrapper->get().get_async_max_queue_size());
        TRY(auto semaphore, Semaphore::create_shared(static_cast<uint32_t>(max_queue_size)));

        output_semaphores.emplace_back(semaphore);
        reader_wrappers.emplace_back(reader_wrapper);
    }

    // Build input wrappers
    std::vector<WriterWrapperPtr<InputStream>> writer_wrappers;
    std::vector<SemaphorePtr> input_semaphores;
    for (auto &input_stream : m_input_streams) {
        TRY(auto writer_wrapper, WriterWrapper<InputStream>::create(input_stream.get(),
            get_params(input_stream.get().name()), m_vdevice, m_overall_latency_meter, m_params.framerate, ASYNC_API));

        TRY(auto max_queue_size, writer_wrapper->get().get_async_max_queue_size());
        TRY(auto semaphore, Semaphore::create_shared(static_cast<uint32_t>(max_queue_size)));

        input_semaphores.emplace_back(semaphore);
        writer_wrappers.emplace_back(writer_wrapper);
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
        TRY(auto wait_index, wait_group.wait_any(HAILORTCLI_DEFAULT_TIMEOUT));

        if (wait_index == shutdown_index) {
            // Stopping the network so we won't get timeout on the flush. The async operations may still be active
            // (until network deactivation).
            stop();
            break;
        } else if ((wait_index >= output_index_start) && (wait_index < input_index_start)) {
            // output is ready
            const size_t output_index = wait_index - output_index_start;
            auto status = reader_wrappers[output_index]->read_async(
                [semaphore=output_semaphores[output_index]](const OutputStream::CompletionInfo &) {
                    (void)semaphore->signal();
                }
            );
            CHECK_SUCCESS(status);
        } else {
            // input is ready
            const size_t input_index = wait_index - input_index_start;
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
    m_cng->shutdown();
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