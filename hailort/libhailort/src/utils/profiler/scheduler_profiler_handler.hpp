/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_profiler_handler.hpp
 * @brief Implementation of the scheduler profiler handlers base with HailoRT tracer mechanism
 **/

#ifndef _HAILO_SCHEDULER_PROFILER_HANDLER_HPP_
#define _HAILO_SCHEDULER_PROFILER_HANDLER_HPP_

#include "hailo/hailort.h"
#include <condition_variable>
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

    SchedulerProfilerHandler(size_t dump_after_n_seconds=0, size_t dump_after_n_kb=0);
    ~SchedulerProfilerHandler();

    virtual void handle_trace(const AddCoreOpTrace&) override;
    virtual void handle_trace(const AddStreamH2DTrace&) override;
    virtual void handle_trace(const AddStreamD2HTrace&) override;
    virtual void handle_trace(const FrameEnqueueH2DTrace&) override;
    virtual void handle_trace(const FrameDequeueH2DTrace&) override;
    virtual void handle_trace(const FrameDequeueD2HTrace&) override;
    virtual void handle_trace(const FrameEnqueueD2HTrace&) override;
    virtual void handle_trace(const ActivateCoreOpTrace&) override;
    virtual void handle_trace(const DeactivateCoreOpTrace&) override;
    virtual void handle_trace(const AddDeviceTrace&) override;
    virtual void handle_trace(const SetCoreOpTimeoutTrace&) override;
    virtual void handle_trace(const SetCoreOpThresholdTrace&) override;
    virtual void handle_trace(const SetCoreOpPriorityTrace&) override;
    virtual void handle_trace(const OracleDecisionTrace&) override;
    virtual void handle_trace(const DumpProfilerStateTrace&) override;
    virtual void handle_trace(const InitProfilerProtoTrace&) override;
    virtual void handle_trace(const HefLoadedTrace&) override;
    virtual bool should_dump_trace_file() override;
    virtual bool should_stop () override { return m_file_already_dumped; }
    virtual hailo_status dump_trace_file() override { return serialize_and_dump_proto(); };

private:
    hailo_status serialize_and_dump_proto();

    ProtoProfiler m_profiler_trace_proto;
    std::mutex m_proto_lock;
    std::mutex m_dump_file_mutex;
    std::mutex m_cv_mutex;
    std::thread m_timer_thread;
    std::condition_variable m_cv;
    size_t m_time_in_seconds_bounded_dump; // if != 0, generate trace file after N seconds
    size_t m_size_in_kb_bounded_dump; // if != 0, generate trace file after N KB
    bool m_file_already_dumped = false;
    bool m_shutting_down = false;
};

}

#endif /* _SCHEDULER_PROFILER_HANDLER_HPP_ */
