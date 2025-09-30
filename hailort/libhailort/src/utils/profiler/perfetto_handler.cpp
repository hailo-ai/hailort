/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file perfetto_handler.cpp
 * @brief Implementation of the Perfetto handler for HailoRT
 **/

#include "perfetto_handler.hpp"
#include <string>
#include <functional>
#include "common/logger_macros.hpp"
#include "common/os_utils.hpp"
#include "common/env_vars.hpp"
#include "perfetto/hailort_perfetto.hpp"


namespace hailort
{

void PerfettoHandler::handle_trace(const AsyncInferStartTrace &trace)
{
    [[maybe_unused]] uint64_t unique_id = trace.pipeline_unique_id | trace.job_id;
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(perfetto::DynamicString(trace.network_name), unique_id, ASYNC_INFER_TRACK, HAILORT_LIBRARY_CATEGORY,
        "job_id", trace.job_id, "pipeline_unique_id", trace.pipeline_unique_id);
}

void PerfettoHandler::handle_trace(const AsyncInferEndTrace &trace)
{
    [[maybe_unused]] uint64_t unique_id = trace.pipeline_unique_id | trace.job_id;
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(perfetto::DynamicString(trace.network_name), unique_id, ASYNC_INFER_TRACK, HAILORT_LIBRARY_CATEGORY);
}

void PerfettoHandler::handle_trace(const RunPushAsyncStartTrace &trace)
{
    std::string event_name = trace.network_name + " - " + trace.element_name;
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(perfetto::DynamicString(event_name), trace.pipeline_unique_id, PIPELINE_TRACK,
        HAILORT_LIBRARY_DETAILED_CATEGORY, "element_name", perfetto::DynamicString(trace.element_name), "pipeline_unique_id", trace.pipeline_unique_id);
}

void PerfettoHandler::handle_trace(const RunPushAsyncEndTrace &trace)
{
    std::string event_name = trace.network_name + " - " + trace.element_name;
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(perfetto::DynamicString(event_name), trace.pipeline_unique_id, PIPELINE_TRACK,
        HAILORT_LIBRARY_DETAILED_CATEGORY);
}

void PerfettoHandler::handle_trace(const SwitchCoreOpStartTrace &trace)
{
    std::string event_name =  trace.network_name + " switch core op";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK,
        HAILORT_LIBRARY_DETAILED_CATEGORY, "network_name", perfetto::DynamicString(trace.network_name),
        "timeout_ms", static_cast<uint64_t>(trace.timeout_ms), "threshold", static_cast<uint32_t>(trace.threshold),
        "batch_size", static_cast<int64_t>(trace.batch_size));
}

void PerfettoHandler::handle_trace(const SwitchCoreOpEndTrace &trace)
{
    std::string event_name =  trace.network_name + " switch core op";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK,
        HAILORT_LIBRARY_DETAILED_CATEGORY);
}

void PerfettoHandler::handle_trace(const SetCoreOpPriorityTrace &trace)
{
    (void)trace;
    std::string event_name = "set core op priority";
    HAILORT_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(event_name), SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY,
        "core_op_handle", static_cast<uint64_t>(trace.core_op_handle), "priority", static_cast<uint64_t>(trace.priority));
    HAILORT_LIBRARY_TRACE_EVENT_END(SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY);
}

void PerfettoHandler::handle_trace(const SetCoreOpThresholdTrace &trace)
{
    (void)trace;
    std::string event_name = "set core op threshold";
    HAILORT_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(event_name), SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY,
        "core_op_handle", static_cast<uint64_t>(trace.core_op_handle), "threshold", static_cast<uint64_t>(trace.threshold));
    HAILORT_LIBRARY_TRACE_EVENT_END(SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY);
}

void PerfettoHandler::handle_trace(const SetCoreOpTimeoutTrace &trace)
{
    (void)trace;
    std::string event_name = "set core op timeout";
    HAILORT_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(event_name), SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY,
        "core_op_handle", static_cast<uint64_t>(trace.core_op_handle), "timeout_ms", static_cast<uint64_t>(trace.timeout.count()));
    HAILORT_LIBRARY_TRACE_EVENT_END(SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY);
}

void PerfettoHandler::handle_trace(const OracleDecisionTrace &trace)
{
    (void)trace;
    std::string event_name = "oracle decision";
    HAILORT_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(event_name), SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY,
        "core_op_handle", static_cast<uint64_t>(trace.core_op_handle), "reason_idle", static_cast<uint64_t>(trace.reason_idle),
        "over_threshold", static_cast<uint64_t>(trace.over_threshold), "over_timeout", static_cast<uint64_t>(trace.over_timeout));
    HAILORT_LIBRARY_TRACE_EVENT_END(SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY);
}

void PerfettoHandler::handle_trace(const PrepareCoreOpStartTrace &trace)
{
    std::string event_name = trace.network_name + " prepare core op";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY,
        "network_name", perfetto::DynamicString(trace.network_name), "n_pending_req", static_cast<uint64_t>(trace.n_pending_req),
        "n_ready_req", static_cast<uint64_t>(trace.n_ready_req), "burst_size", static_cast<uint64_t>(trace.burst_size));
}

void PerfettoHandler::handle_trace(const PrepareCoreOpEndTrace &trace)
{
    std::string event_name = trace.network_name + " prepare core op";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY);
}

void PerfettoHandler::handle_trace(const SchedulerInferAsyncStartTrace &trace)
{
    std::string event_name = trace.network_name + " infer async";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK, HAILORT_LIBRARY_CATEGORY,
        "network_name", perfetto::DynamicString(trace.network_name), "n_pending_req", static_cast<uint64_t>(trace.n_pending_req),
        "n_ready_req", static_cast<uint64_t>(trace.n_ready_req), "n_ongoing_req", static_cast<uint64_t>(trace.n_ongoing_req));
}

void PerfettoHandler::handle_trace(const SchedulerInferAsyncEndTrace &trace)
{
    std::string event_name = trace.network_name + " infer async";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK, HAILORT_LIBRARY_CATEGORY);
}

void PerfettoHandler::handle_trace(const SchedulerEnqueueInferRequestStartTrace &trace)
{
    std::string event_name = trace.network_name + " enqueue infer request";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY,
        "network_name", perfetto::DynamicString(trace.network_name), "n_pending_req", static_cast<uint64_t>(trace.n_pending_req));
}

void PerfettoHandler::handle_trace(const SchedulerEnqueueInferRequestEndTrace &trace)
{
    std::string event_name = trace.network_name + " enqueue infer request";
    HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(perfetto::DynamicString(event_name), trace.unique_id, SCHEDULER_TRACK, HAILORT_LIBRARY_DETAILED_CATEGORY);
}
}
