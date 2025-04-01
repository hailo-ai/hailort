/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "utils/query_stats_utils.hpp"
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

        // m_handlers might be modified by other threads so the loop is protected by a mutex
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_handlers.begin(); it != m_handlers.end();) {
                (*it)->handle_trace(trace_struct);

                if ((*it)->should_dump_trace_file()) {
                    (*it)->dump_trace_file();
                }

                if ((*it)->should_stop()) {
                    it = m_handlers.erase(it);
                } else {
                    it++;
                }
            }
        }
    }

    bool m_should_trace = false;
    bool m_should_monitor = false;
    std::chrono::high_resolution_clock::time_point m_start_time;
    std::vector<std::unique_ptr<Handler>> m_handlers;
    std::mutex m_mutex;
};

}

#endif
