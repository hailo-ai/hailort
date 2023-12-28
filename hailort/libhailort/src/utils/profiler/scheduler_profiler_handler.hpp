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
#pragma GCC diagnostic ignored "-Wunused-parameter"
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
    virtual void handle_trace(const AddStreamH2DTrace&) override;
    virtual void handle_trace(const AddStreamD2HTrace&) override;
    virtual void handle_trace(const FrameEnqueueH2DTrace&) override;
    virtual void handle_trace(const FrameDequeueH2DTrace&) override;
    virtual void handle_trace(const FrameDequeueD2HTrace&) override;
    virtual void handle_trace(const FrameEnqueueD2HTrace&) override;
    virtual void handle_trace(const SwitchCoreOpTrace&) override;
    virtual void handle_trace(const AddDeviceTrace&) override;
    virtual void handle_trace(const SetCoreOpTimeoutTrace&) override;
    virtual void handle_trace(const SetCoreOpThresholdTrace&) override;
    virtual void handle_trace(const SetCoreOpPriorityTrace&) override;
    virtual void handle_trace(const OracleDecisionTrace&) override;
    virtual void handle_trace(const DumpProfilerStateTrace&) override;
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
};

}

#endif /* _SCHEDULER_PROFILER_HANDLER_HPP_ */