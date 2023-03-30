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

#include "vdevice/scheduler/scheduler_base.hpp"

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

struct AddCoreOpTrace : Trace
{
    AddCoreOpTrace(const std::string &device_id, const std::string &core_op_name, uint64_t timeout, uint32_t threshold, scheduler_core_op_handle_t handle)
        : Trace("add_core_op"), device_id(device_id), core_op_name(core_op_name), timeout(timeout), threshold(threshold), core_op_handle(handle)
    {}

    std::string device_id;
    std::string core_op_name;
    uint64_t timeout = 0;
    uint32_t threshold = 0;
    scheduler_core_op_handle_t core_op_handle = INVALID_CORE_OP_HANDLE;
};

struct CreateCoreOpInputStreamsTrace : Trace
{
    CreateCoreOpInputStreamsTrace(const std::string &device_id, const std::string &core_op_name, const std::string &stream_name, uint32_t queue_size)
        : Trace("create_input_stream"), device_id(device_id), core_op_name(core_op_name), stream_name(stream_name), queue_size(queue_size)
    {}

    std::string device_id;
    std::string core_op_name;
    std::string stream_name;
    uint32_t queue_size;
};

struct CreateCoreOpOutputStreamsTrace : Trace
{
    CreateCoreOpOutputStreamsTrace(const std::string &device_id, const std::string &core_op_name, const std::string &stream_name, uint32_t queue_size)
        : Trace("create_output_stream"), device_id(device_id), core_op_name(core_op_name), stream_name(stream_name), queue_size(queue_size)
    {}

    std::string device_id;
    std::string core_op_name;
    std::string stream_name;
    uint32_t queue_size;
};

struct WriteFrameTrace : Trace
{
    WriteFrameTrace(const std::string &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("write_frame"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    std::string device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct InputVdmaDequeueTrace : Trace
{
    InputVdmaDequeueTrace(const std::string &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("input_vdma_dequeue"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    std::string device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct ReadFrameTrace : Trace
{
    ReadFrameTrace(const std::string &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("read_frame"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    std::string device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct OutputVdmaEnqueueTrace : Trace
{
    OutputVdmaEnqueueTrace(const std::string &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name, uint32_t frames)
        : Trace("output_vdma_enqueue"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name), frames(frames)
    {}

    std::string device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
    uint32_t frames = 0;
};

struct ChooseCoreOpTrace : Trace
{
    ChooseCoreOpTrace(const std::string &device_id, scheduler_core_op_handle_t handle, bool threshold, bool timeout, core_op_priority_t priority)
        : Trace("choose_core_op"), device_id(device_id), core_op_handle(handle), threshold(threshold), timeout(timeout), priority(priority)
    {}

    std::string device_id;
    scheduler_core_op_handle_t core_op_handle;
    bool threshold = false;
    bool timeout = false;
    core_op_priority_t priority;
};

struct SwitchCoreOpTrace : Trace
{
    SwitchCoreOpTrace(const std::string &device_id, scheduler_core_op_handle_t handle)
        : Trace("switch_core_op"), device_id(device_id), core_op_handle(handle)
    {}

    std::string device_id;
    scheduler_core_op_handle_t core_op_handle;
};

class Handler
{
public:
    virtual ~Handler() = default;

    virtual void handle_trace(const InitTrace&) {};
    virtual void handle_trace(const AddCoreOpTrace&) {};
    virtual void handle_trace(const CreateCoreOpInputStreamsTrace&) {};
    virtual void handle_trace(const CreateCoreOpOutputStreamsTrace&) {};
    virtual void handle_trace(const WriteFrameTrace&) {};
    virtual void handle_trace(const InputVdmaDequeueTrace&) {};
    virtual void handle_trace(const ReadFrameTrace&) {};
    virtual void handle_trace(const OutputVdmaEnqueueTrace&) {};
    virtual void handle_trace(const ChooseCoreOpTrace&) {};
    virtual void handle_trace(const SwitchCoreOpTrace&) {};
};

struct JSON;

class SchedulerProfilerHandler : public Handler
{
public:
    SchedulerProfilerHandler(SchedulerProfilerHandler const&) = delete;
    void operator=(SchedulerProfilerHandler const&) = delete;

    SchedulerProfilerHandler(int64_t &start_time);
    ~SchedulerProfilerHandler();

    virtual void handle_trace(const AddCoreOpTrace&) override;
    virtual void handle_trace(const CreateCoreOpInputStreamsTrace&) override;
    virtual void handle_trace(const CreateCoreOpOutputStreamsTrace&) override;
    virtual void handle_trace(const WriteFrameTrace&) override;
    virtual void handle_trace(const InputVdmaDequeueTrace&) override;
    virtual void handle_trace(const ReadFrameTrace&) override;
    virtual void handle_trace(const OutputVdmaEnqueueTrace&) override;
    virtual void handle_trace(const ChooseCoreOpTrace&) override;
    virtual void handle_trace(const SwitchCoreOpTrace&) override;

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
        trace_struct.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(curr_time - this->m_start_time).count();
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