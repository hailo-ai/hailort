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

#include "scheduler_profiler_handler.hpp"
#include "monitor_handler.hpp"
namespace hailort
{
class Tracer
{
public:
    Tracer();
    template<class TraceType, typename... Args>
    static void trace(Args... trace_args)
    {
        auto &tracer = get_instance();
        tracer->execute_trace<TraceType>(trace_args...);
    }

    static std::unique_ptr<Tracer> &get_instance()
    {
        static std::unique_ptr<Tracer> tracer = nullptr;
        if (nullptr == tracer) {
            tracer = make_unique_nothrow<Tracer>();
        }
        return tracer;
    }

private:
    void init_monitor_handler();
    void init_scheduler_profiler_handler();
    template<class TraceType, typename... Args>
    void execute_trace(Args... trace_args)
    {
        if ((!m_should_trace) && (!m_should_monitor)) {
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
    bool m_should_monitor = false;
    std::chrono::high_resolution_clock::time_point m_start_time;
    std::vector<std::unique_ptr<Handler>> m_handlers;
};

}

#endif