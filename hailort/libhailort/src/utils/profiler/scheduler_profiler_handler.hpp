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
    virtual void handle_trace(const ChooseCoreOpTrace&) override;
    virtual void handle_trace(const SwitchCoreOpTrace&) override;

private:
    void log(JSON json);
    bool comma();

    std::shared_ptr<spdlog::sinks::sink> m_file_sink;
    std::shared_ptr<spdlog::logger> m_profiler_logger;
    std::atomic<bool> m_first_write;
};

}

#endif /* _SCHEDULER_PROFILER_HANDLER_HPP_ */