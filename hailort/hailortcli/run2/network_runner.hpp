/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_runner.hpp
 * @brief Run network on hailo device
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_NETWORK_RUNNER_HPP_
#define _HAILO_HAILORTCLI_RUN2_NETWORK_RUNNER_HPP_

#include "io_wrappers.hpp"
#include "live_stats.hpp"
#include "network_live_track.hpp"

#include "../hailortcli.hpp"

#include "common/barrier.hpp"
#include "common/async_thread.hpp"
#include "common/event_internal.hpp"

#include "hailo/vdevice.hpp"
#include "hailo/vstream.hpp"
#include "hailo/event.hpp"
#include "hailo/network_group.hpp"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include <string>
#include <vector>

using namespace hailort;

constexpr std::chrono::milliseconds SYNC_EVENT_TIMEOUT(1000);


enum class InferenceMode {
    FULL,

    RAW,
    RAW_ASYNC,
    RAW_ASYNC_SINGLE_THREAD,
};

struct IoParams
{
    IoParams();

    std::string name;
    std::string input_file_path;
};

struct VStreamParams : public IoParams
{
    VStreamParams();

    hailo_vstream_params_t params;
};

struct StreamParams : public IoParams
{
    StreamParams();

    hailo_stream_flags_t flags;
};

struct NetworkParams
{
    NetworkParams();

    std::string hef_path;
    std::string net_group_name;
    std::vector<VStreamParams> vstream_params;
    std::vector<StreamParams> stream_params;
    hailo_scheduling_algorithm_t scheduling_algorithm;

    // Network parameters
    uint16_t batch_size;
    uint32_t scheduler_threshold;
    uint32_t scheduler_timeout_ms;
    uint8_t scheduler_priority;

    // Run parameters
    uint32_t framerate;

    bool measure_hw_latency;
    bool measure_overall_latency;
    InferenceMode mode;

    bool is_async() const
    {
        return (mode == InferenceMode::RAW_ASYNC) || (mode == InferenceMode::RAW_ASYNC_SINGLE_THREAD);
    }
};

class SignalEventScopeGuard final
{
public:
    SignalEventScopeGuard(Event &event);
    ~SignalEventScopeGuard();

private:
    Event &m_event;
};

class BarrierTerminateScopeGuard final
{
public:
    BarrierTerminateScopeGuard(BarrierPtr barrier);
    ~BarrierTerminateScopeGuard();

private:
    BarrierPtr m_barrier;
};

class NetworkRunner
{
public:
    static Expected<std::shared_ptr<NetworkRunner>> create_shared(VDevice &vdevice, const NetworkParams &params);

    NetworkRunner(const NetworkParams &params, const std::string &name,
        VDevice &vdevice, std::shared_ptr<ConfiguredNetworkGroup> cng);
    virtual ~NetworkRunner() = default;

    hailo_status run(EventPtr shutdown_event, LiveStats &live_stats, Barrier &activation_barrier);
    virtual void stop() = 0;
    // Must be called prior to run
    void set_overall_latency_meter(LatencyMeterPtr latency_meter);
    void set_latency_barrier(BarrierPtr latency_barrier);
    std::shared_ptr<ConfiguredNetworkGroup> get_configured_network_group();
    void set_last_measured_fps(double fps);
    double get_last_measured_fps();

protected:
    static bool inference_succeeded(hailo_status status);
    // Use 'inference_succeeded(async_thread->get())' to check for a thread's success
    virtual Expected<std::vector<AsyncThreadPtr<hailo_status>>> start_inference_threads(EventPtr shutdown_event,
        std::shared_ptr<NetworkLiveTrack> net_live_track) = 0;
    virtual hailo_status run_single_thread_async_infer(EventPtr shutdown_event,
        std::shared_ptr<NetworkLiveTrack> net_live_track) = 0;

    virtual std::set<std::string> get_input_names() = 0;
    virtual std::set<std::string> get_output_names() = 0;

    static Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> create_vstreams(
        ConfiguredNetworkGroup &net_group, const std::map<std::string, hailo_vstream_params_t> &params);

    template <typename Writer>
    hailo_status run_write(WriterWrapperPtr<Writer> writer, EventPtr shutdown_event,
        std::shared_ptr<Barrier> latency_barrier)
    {
        auto latency_barrier_scope_guard = BarrierTerminateScopeGuard(latency_barrier);
        auto signal_event_scope_guard = SignalEventScopeGuard(*shutdown_event);

        while (true) {
            if (latency_barrier) {
                latency_barrier->arrive_and_wait();
            }

            for (auto i = 0; i < m_params.batch_size; i++) {
                auto status = writer->write();
                if (status == HAILO_STREAM_ABORTED_BY_USER) {
                    return status;
                }
                CHECK_SUCCESS(status);
            }
        }
        return HAILO_SUCCESS;
    }

    template <typename Writer>
    hailo_status run_write_async(WriterWrapperPtr<Writer> writer, EventPtr shutdown_event,
        std::shared_ptr<Barrier> latency_barrier)
    {
        auto latency_barrier_scope_guard = BarrierTerminateScopeGuard(latency_barrier);
        auto signal_event_scope_guard = SignalEventScopeGuard(*shutdown_event);

        // When measuring latency we want to send one frame at a time (to avoid back-pressure)
        // sync_event will be used to send one frame at a time
        EventPtr sync_event = nullptr;
        if (m_params.measure_hw_latency || m_params.measure_overall_latency) {
            auto sync_event_exp = Event::create_shared(Event::State::not_signalled);
            CHECK_EXPECTED_AS_STATUS(sync_event_exp);
            sync_event = sync_event_exp.release();
        }

        while (true) {
            if (latency_barrier) {
                latency_barrier->arrive_and_wait();
            }

            for (auto i = 0; i < m_params.batch_size; i++) {
                auto status = writer->wait_for_async_ready();
                if (status == HAILO_STREAM_ABORTED_BY_USER) {
                    return status;
                }
                CHECK_SUCCESS(status);

                status = writer->write_async(
                    [sync_event](const typename Writer::CompletionInfo &) {
                        if (sync_event) {
                            (void)sync_event->signal();
                        }
                    });
                if (status == HAILO_STREAM_ABORTED_BY_USER) {
                    return status;
                }
                CHECK_SUCCESS(status);

                if (m_params.measure_hw_latency || m_params.measure_overall_latency) {
                    status = WaitOrShutdown(sync_event, shutdown_event).wait(SYNC_EVENT_TIMEOUT);
                    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
                        // Don't print an error for this
                        return status;
                    }
                    CHECK_SUCCESS(status);
                    status = sync_event->reset();
                    CHECK_SUCCESS(status);
                }
            }
        }
        return HAILO_SUCCESS;
    }

    template <typename Reader>
    hailo_status run_read(ReaderWrapperPtr<Reader> reader, EventPtr shutdown_event,
        std::shared_ptr<Barrier> latency_barrier)
    {
        auto latency_barrier_scope_guard = BarrierTerminateScopeGuard(latency_barrier);
        auto signal_event_scope_guard = SignalEventScopeGuard(*shutdown_event);

        while (true) {
            if (latency_barrier) {
                latency_barrier->arrive_and_wait();
            }

            for (auto i = 0; i < m_params.batch_size; i++) {
                auto status = reader->read();
                if (status == HAILO_STREAM_ABORTED_BY_USER) {
                    return status;
                }
                CHECK_SUCCESS(status);
            }
        }
        return HAILO_SUCCESS;
    }

    template <typename Reader>
    hailo_status run_read_async(ReaderWrapperPtr<Reader> reader, EventPtr shutdown_event,
        std::shared_ptr<Barrier> latency_barrier)
    {
        auto latency_barrier_scope_guard = BarrierTerminateScopeGuard(latency_barrier);
        auto signal_event_scope_guard = SignalEventScopeGuard(*shutdown_event);

        // When measuring latency we want to send one frame at a time (to avoid back-pressure)
        // sync_event will be used to send one frame at a time
        EventPtr sync_event = nullptr;
        if (m_params.measure_hw_latency || m_params.measure_overall_latency) {
            auto sync_event_exp = Event::create_shared(Event::State::not_signalled);
            CHECK_EXPECTED_AS_STATUS(sync_event_exp);
            sync_event = sync_event_exp.release();
        }

        while (true) {
            if (latency_barrier) {
                latency_barrier->arrive_and_wait();
            }

            for (auto i = 0; i < m_params.batch_size; i++) {
                auto status = reader->wait_for_async_ready();
                if (status == HAILO_STREAM_ABORTED_BY_USER) {
                    return status;
                }
                CHECK_SUCCESS(status);

                status = reader->read_async(
                    [sync_event](const typename Reader::CompletionInfo &) {
                        if (sync_event) {
                            (void)sync_event->signal();
                        }
                    });
                if (status == HAILO_STREAM_ABORTED_BY_USER) {
                    return status;
                }
                CHECK_SUCCESS(status);

                if (m_params.measure_hw_latency || m_params.measure_overall_latency) {
                    status = WaitOrShutdown(sync_event, shutdown_event).wait(SYNC_EVENT_TIMEOUT);
                    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
                        // Don't print an error for this
                        return status;
                    }
                    CHECK_SUCCESS(status);
                    status = sync_event->reset();
                    CHECK_SUCCESS(status);
                }
            }
        }
        return HAILO_SUCCESS;
    }

    VDevice &m_vdevice;
    const NetworkParams m_params;
    std::string m_name;
    std::shared_ptr<ConfiguredNetworkGroup> m_cng;
    LatencyMeterPtr m_overall_latency_meter;
    BarrierPtr m_latency_barrier;
    double m_last_measured_fps;

private:
    static const std::vector<hailo_status> ALLOWED_INFERENCE_RETURN_VALUES;
    static hailo_status wait_for_threads(std::vector<AsyncThreadPtr<hailo_status>> &threads);
    static Expected<BufferPtr> create_constant_dataset(size_t size);
    static Expected<BufferPtr> create_dataset_from_input_file(const std::string &file_path, size_t size);
};

class FullNetworkRunner : public NetworkRunner
{
public:
    FullNetworkRunner(const NetworkParams &params, const std::string &name, VDevice &vdevice,
        std::vector<InputVStream> &&input_vstreams, std::vector<OutputVStream> &&output_vstreams,
        std::shared_ptr<ConfiguredNetworkGroup> cng);

    virtual Expected<std::vector<AsyncThreadPtr<hailo_status>>> start_inference_threads(EventPtr shutdown_event,
        std::shared_ptr<NetworkLiveTrack> net_live_track) override;
    virtual hailo_status run_single_thread_async_infer(EventPtr, std::shared_ptr<NetworkLiveTrack>) override
    {
        return HAILO_NOT_IMPLEMENTED;
    };

    virtual void stop() override;
    virtual std::set<std::string> get_input_names() override;
    virtual std::set<std::string> get_output_names() override;
    VStreamParams get_params(const std::string &name);

private:
    std::vector<InputVStream> m_input_vstreams;
    std::vector<OutputVStream> m_output_vstreams;
};

class RawNetworkRunner : public NetworkRunner
{
public:
    RawNetworkRunner(const NetworkParams &params, const std::string &name, VDevice &vdevice,
        InputStreamRefVector &&input_streams, OutputStreamRefVector &&output_streams,
        std::shared_ptr<ConfiguredNetworkGroup> cng);

    virtual Expected<std::vector<AsyncThreadPtr<hailo_status>>> start_inference_threads(EventPtr shutdown_event,
        std::shared_ptr<NetworkLiveTrack> net_live_track) override;

    virtual hailo_status run_single_thread_async_infer(EventPtr shutdown_event,
        std::shared_ptr<NetworkLiveTrack> net_live_track) override;

    virtual void stop() override;
    virtual std::set<std::string> get_input_names() override;
    virtual std::set<std::string> get_output_names() override;
    StreamParams get_params(const std::string &name);

private:
    InputStreamRefVector m_input_streams;
    OutputStreamRefVector m_output_streams;
};

#endif /* _HAILO_HAILORTCLI_RUN2_NETWORK_RUNNER_HPP_ */