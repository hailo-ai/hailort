/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_profiler_handler.hpp
 * @brief Implementation of the scheduler profiler handlers base with HailoRT tracer mechanism
 **/

#ifndef _HAILO_SCHEDULER_PROFILER_HANDLER_HPP_
#define _HAILO_SCHEDULER_PROFILER_HANDLER_HPP_

#include "hailo/hailort.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "tracer_profiler.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop )
#else
#pragma GCC diagnostic pop
#endif

#include "handler.hpp"

namespace hailort
{
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
    virtual void handle_trace(const SwitchCoreOpTrace&) override;
    virtual void handle_trace(const AddDeviceTrace&) override;
    virtual void handle_trace(const SetCoreOpTimeoutTrace&) override;
    virtual void handle_trace(const SetCoreOpThresholdTrace&) override;
    virtual void handle_trace(const SetCoreOpPriorityTrace&) override;
    virtual void handle_trace(const OracleDecisionTrace&) override;
    virtual void handle_trace(const DumpProfilerState&) override;
    virtual void handle_trace(const InitProfilerProtoTrace&) override;

private:
    void log(JSON json);
    bool comma();
    void serialize_and_dump_proto();

    std::shared_ptr<spdlog::sinks::sink> m_file_sink;
    std::shared_ptr<spdlog::logger> m_profiler_logger;
    std::atomic<bool> m_first_write;
    ProtoProfiler m_profiler_trace_proto;
    std::mutex m_proto_lock;
    int64_t m_start_time;
};

}

#endif /* _SCHEDULER_PROFILER_HANDLER_HPP_ */