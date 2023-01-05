/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracer.hpp
 * @brief Tracing mechanism for HailoRT + FW events
 **/

#ifndef _HAILO_TRACER_HPP_
#define _HAILO_TRACER_HPP_

#include "hailo/hailort.h"
#include "common/logger_macros.hpp"
#include "network_group_scheduler.hpp"

#include <chrono>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>


namespace hailort
{

struct Trace
{
    Trace(const std::string &name)
        : name(name)
    {}

    virtual ~Trace() = default;

    uint64_t timestamp = 0;
    std::string name;
};

struct InitTrace : Trace
{
    InitTrace() : Trace("init") {}
};

struct AddNetworkGroupTrace : Trace
{
    AddNetworkGroupTrace(const std::string &device_id, const std::string &network_group_name, uint64_t timeout, uint32_t threshold, scheduler_ng_handle_t handle)
        : Trace("add_network_group"), device_id(device_id), network_group_name(network_group_name), timeout(timeout), threshold(threshold), network_group_handle(handle)
    {}

    std::string device_id;
    std::string network_group_name;
    uint64_t timeout = 0;
    uint32_t threshold = 0;
    scheduler_ng_handle_t network_group_handle = INVALID_NETWORK_GROUP_HANDLE;
};

struct CreateNetworkGroupInputStreamsTrace : Trace
{
    CreateNetworkGroupInputStreamsTrace(const std::string &device_id, const std::string &network_group_name, const std::string &stream_name, uint32_t queue_size)
        : Trace("create_input_stream"), device_id(device_id), network_group_name(network_group_name), stream_name(stream_name), queue_size(queue_size)
    {}

    std::string device_id;
    std::string network_group_name;
    std::string stream_name;
    uint32_t queue_size;
};

struct CreateNetworkGroupOutputStreamsTrace : Trace
{
    CreateNetworkGroupOutputStreamsTrace(const std::string &device_id, const std::string &network_group_name, const std::string &stream_name, uint32_t queue_size)
        : Trace("create_output_stream"), device_id(device_id), network_group_name(network_group_name), stream_name(stream_name), queue_size(queue_size)
    {}

    std::string device_id;
    std::string network_group_name;
    std::string stream_name;
    uint32_t queue_size;
};

struct WriteFrameTrace : Trace
{
    WriteFrameTrace(const std::string &device_id, scheduler_ng_handle_t network_group_handle, const std::string &queue_name)
        : Trace("wrte_frame"), device_id(device_id), network_group_handle(network_group_handle), queue_name(queue_name)
    {}

    std::string device_id;
    scheduler_ng_handle_t network_group_handle;
    std::string queue_name;
};

struct InputVdmaEnqueueTrace : Trace
{
    InputVdmaEnqueueTrace(const std::string &device_id, scheduler_ng_handle_t network_group_handle, const std::string &queue_name)
        : Trace("input_vdma_enqueue"), device_id(device_id), network_group_handle(network_group_handle), queue_name(queue_name)
    {}

    std::string device_id;
    scheduler_ng_handle_t network_group_handle;
    std::string queue_name;
};

struct ReadFrameTrace : Trace
{
    ReadFrameTrace(const std::string &device_id, scheduler_ng_handle_t network_group_handle, const std::string &queue_name)
        : Trace("read_frame"), device_id(device_id), network_group_handle(network_group_handle), queue_name(queue_name)
    {}

    std::string device_id;
    scheduler_ng_handle_t network_group_handle;
    std::string queue_name;
};

struct OutputVdmaEnqueueTrace : Trace
{
    OutputVdmaEnqueueTrace(const std::string &device_id, scheduler_ng_handle_t network_group_handle, const std::string &queue_name, uint32_t frames)
        : Trace("output_vdma_enqueue"), device_id(device_id), network_group_handle(network_group_handle), queue_name(queue_name), frames(frames)
    {}

    std::string device_id;
    scheduler_ng_handle_t network_group_handle;
    std::string queue_name;
    uint32_t frames = 0;
};

struct ChooseNetworkGroupTrace : Trace
{
    ChooseNetworkGroupTrace(const std::string &device_id, scheduler_ng_handle_t handle, bool threshold, bool timeout)
        : Trace("choose_network_group"), device_id(device_id), network_group_handle(handle), threshold(threshold), timeout(timeout)
    {}

    std::string device_id;
    scheduler_ng_handle_t network_group_handle;
    bool threshold = false;
    bool timeout = false;
};

struct SwitchNetworkGroupTrace : Trace
{
    SwitchNetworkGroupTrace(const std::string &device_id, scheduler_ng_handle_t handle)
        : Trace("switch_network_group"), device_id(device_id), network_group_handle(handle)
    {}

    std::string device_id;
    scheduler_ng_handle_t network_group_handle;
};

class Handler
{
public:
    virtual ~Handler() = default;

    virtual void handle_trace(const InitTrace&) {};
    virtual void handle_trace(const AddNetworkGroupTrace&) {};
    virtual void handle_trace(const CreateNetworkGroupInputStreamsTrace&) {};
    virtual void handle_trace(const CreateNetworkGroupOutputStreamsTrace&) {};
    virtual void handle_trace(const WriteFrameTrace&) {};
    virtual void handle_trace(const InputVdmaEnqueueTrace&) {};
    virtual void handle_trace(const ReadFrameTrace&) {};
    virtual void handle_trace(const OutputVdmaEnqueueTrace&) {};
    virtual void handle_trace(const ChooseNetworkGroupTrace&) {};
    virtual void handle_trace(const SwitchNetworkGroupTrace&) {};
};

struct JSON;

class SchedulerProfilerHandler : public Handler
{
public:
    SchedulerProfilerHandler(SchedulerProfilerHandler const&) = delete;
    void operator=(SchedulerProfilerHandler const&) = delete;

    SchedulerProfilerHandler(int64_t &start_time);
    ~SchedulerProfilerHandler();

    virtual void handle_trace(const AddNetworkGroupTrace&) override;
    virtual void handle_trace(const CreateNetworkGroupInputStreamsTrace&) override;
    virtual void handle_trace(const CreateNetworkGroupOutputStreamsTrace&) override;
    virtual void handle_trace(const WriteFrameTrace&) override;
    virtual void handle_trace(const InputVdmaEnqueueTrace&) override;
    virtual void handle_trace(const ReadFrameTrace&) override;
    virtual void handle_trace(const OutputVdmaEnqueueTrace&) override;
    virtual void handle_trace(const ChooseNetworkGroupTrace&) override;
    virtual void handle_trace(const SwitchNetworkGroupTrace&) override;

private:
    void log(JSON json);
    bool comma();

    std::shared_ptr<spdlog::sinks::sink> m_file_sink;
    std::shared_ptr<spdlog::logger> m_profiler_logger;
    std::atomic<bool> m_first_write;
};

class Tracer
{
public:
    template<class TraceType, typename... Args>
    static void trace(Args... trace_args)
    {
        auto &tracer = get_instance();
        tracer.execute_trace<TraceType>(trace_args...);
    }

private:
    Tracer();

    static Tracer& get_instance()
    {
        static Tracer tracer;
        return tracer;
    }

    template<class TraceType, typename... Args>
    void execute_trace(Args... trace_args)
    {
        if (!m_should_trace) {
            return;
        }

        TraceType trace_struct(trace_args...);
        auto curr_time = std::chrono::high_resolution_clock::now();
        trace_struct.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - this->m_start_time).count();
        for (auto &handler : this->m_handlers) {
            handler->handle_trace(trace_struct);
        }
    }

    bool m_should_trace = false;
    std::chrono::high_resolution_clock::time_point m_start_time;
    std::vector<std::unique_ptr<Handler>> m_handlers;
};

}

#endif