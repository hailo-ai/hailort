/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_profiler_handler.cpp
 * @brief Implementation of the scheduler profiler handlers base with HailoRT tracer mechanism
 **/

#include "scheduler_profiler_handler.hpp"
#include "profiler_utils.hpp"

#include "common/logger_macros.hpp"

#include "utils/hailort_logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/null_sink.h>

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <fstream>
#include <iomanip>

#define PROFILER_DEFAULT_FILE_NAME ("hailo.tracer")
#define SCHEDULER_PROFILER_NAME ("SchedulerProfiler")
#define PROFILER_FILE_ENV_VAR ("HAILO_TRACE_FILE")
#define SCHEDULER_PROFILER_LOGGER_FILENAME ("scheduler_profiler.json")
#define SCHEDULER_PROFILER_LOGGER_PATTERN ("%v")

#define SCHEDULER_PROFILER_LOGGER_PATH ("SCHEDULER_PROFILER_LOGGER_PATH")

namespace hailort
{

SchedulerProfilerHandler::SchedulerProfilerHandler(int64_t &start_time)
#ifndef __ANDROID__
    : m_file_sink(HailoRTLogger::create_file_sink(HailoRTLogger::get_log_path(SCHEDULER_PROFILER_LOGGER_PATH), SCHEDULER_PROFILER_LOGGER_FILENAME, false)),
      m_first_write(true)
#endif
{
#ifndef __ANDROID__
    spdlog::sinks_init_list sink_list = { m_file_sink };
    m_profiler_logger = make_shared_nothrow<spdlog::logger>(SCHEDULER_PROFILER_NAME, sink_list.begin(), sink_list.end());
    m_file_sink->set_level(spdlog::level::level_enum::info);
    m_file_sink->set_pattern(SCHEDULER_PROFILER_LOGGER_PATTERN);
    std::stringstream ss;
    ss << "{\"ns_since_epoch_zero_time\": \"" << start_time << "\",\n\"scheduler_actions\": [\n";
    m_profiler_logger->info(ss.str());
#else
    (void)start_time;
#endif
}

SchedulerProfilerHandler::~SchedulerProfilerHandler()
{
    m_profiler_logger->info("]\n}");
}

void SchedulerProfilerHandler::serialize_and_dump_proto()
{
    auto file_env_var = std::getenv(PROFILER_FILE_ENV_VAR);
    std::string file_name = PROFILER_DEFAULT_FILE_NAME;
    if (nullptr != file_env_var) {
        file_name = std::string(file_env_var);
    }

    std::ofstream output_file(std::string(file_name), std::ios::out |std::ios::binary);
    google::protobuf::io::OstreamOutputStream stream(&output_file);

    if(!m_profiler_trace_proto.SerializeToZeroCopyStream(&stream)) {
        LOGGER__ERROR("Failed writing profiling data to file {}.", file_name);
    }
}

struct JSON
{
    std::unordered_map<std::string, std::string> members;
    JSON(const std::initializer_list<std::pair<const std::string, std::string>> &dict) : members{dict} {}
    JSON(const std::unordered_map<std::string, uint32_t> &dict) {
        for (auto &pair : dict) {
            members.insert({pair.first, std::to_string(pair.second)});
        }
    }
};

template<class T>
std::string json_to_string(const T &val) {
    return std::to_string(val);
}

template<>
std::string json_to_string(const std::string &val) {
    std::ostringstream os;
    os << std::quoted(val);
    return os.str();
}

template<>
std::string json_to_string(const bool &bool_val) {
    return bool_val ? "true" : "false";
}

template<>
std::string json_to_string(const JSON &json_val) {
    std::ostringstream os;
    os << "{\n";
    size_t i = 0;
    for (const auto &kv : json_val.members) {
        ++i;
        os << std::quoted(kv.first) << " : ";
        os << kv.second;
        if (i != json_val.members.size()) {
            os << ",\n";
        }
    }
    os << "\n}";
    return os.str();
}

bool SchedulerProfilerHandler::comma()
{
    auto result = !m_first_write;
    m_first_write = false;
    return result;
}

void SchedulerProfilerHandler::log(JSON json)
{
    m_profiler_logger->info("{}{}", comma() ? ",\n" : "", json_to_string(json));
}

void SchedulerProfilerHandler::handle_trace(const InitProfilerProtoTrace &trace)
{
    ProfilerTime curr_time = get_curr_time();

    auto init = m_profiler_trace_proto.mutable_top_header();
    #if defined(__linux__)
    init->set_os_name(os_name());
    init->set_os_ver(os_ver());
    init->set_cpu_arch(cpu_arch());
    init->set_sys_ram_size(system_ram_size());
    #endif
    init->set_hailort_ver(get_libhailort_version_representation());
    init->mutable_time()->set_day(curr_time.day);
    init->mutable_time()->set_month(curr_time.month);
    init->mutable_time()->set_year(curr_time.year);
    init->mutable_time()->set_hour(curr_time.hour);
    init->mutable_time()->set_min(curr_time.min);
    init->set_time_stamp(trace.timestamp);
    init->set_time_stamp_since_epoch(curr_time.time_since_epoch);
}

void SchedulerProfilerHandler::handle_trace(const AddCoreOpTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"core_op_name", json_to_string(trace.core_op_name)},
        {"core_op_handle", json_to_string(trace.core_op_handle)},
        {"timeout", json_to_string((uint64_t)trace.timeout)},
        {"threshold", json_to_string((uint64_t)trace.threshold)},
        {"max_batch_size", json_to_string((uint64_t)trace.batch_size)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_added_core_op()->set_time_stamp(trace.timestamp);
    added_trace->mutable_added_core_op()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_added_core_op()->set_core_op_name(trace.core_op_name);
    added_trace->mutable_added_core_op()->set_max_batch_size(trace.batch_size);
}

void SchedulerProfilerHandler::handle_trace(const AddDeviceTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_added_device()->mutable_device_info()->set_device_id(trace.device_id);
    added_trace->mutable_added_device()->mutable_device_info()->set_device_arch(trace.device_arch);
    added_trace->mutable_added_device()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const AddStreamH2DTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"core_op_name", json_to_string(trace.core_op_name)},
        {"stream_name", json_to_string(trace.stream_name)},
        {"queue_size", json_to_string(trace.queue_size)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_added_stream()->set_device_id(trace.device_id);
    added_trace->mutable_added_stream()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__H2D);
    added_trace->mutable_added_stream()->set_queue_size(trace.queue_size);
    added_trace->mutable_added_stream()->set_stream_name(trace.stream_name);
    added_trace->mutable_added_stream()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_added_stream()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const AddStreamD2HTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"core_op_name", json_to_string(trace.core_op_name)},
        {"stream_name", json_to_string(trace.stream_name)},
        {"queue_size", json_to_string(trace.queue_size)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_added_stream()->set_device_id(trace.device_id);
    added_trace->mutable_added_stream()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__D2H);
    added_trace->mutable_added_stream()->set_stream_name(trace.stream_name);
    added_trace->mutable_added_stream()->set_queue_size(trace.queue_size);
    added_trace->mutable_added_stream()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_added_stream()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const FrameEnqueueH2DTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"core_op_handle", json_to_string(trace.core_op_handle)},
        {"queue_name", json_to_string(trace.queue_name)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_frame_enqueue()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__H2D);
    added_trace->mutable_frame_enqueue()->set_stream_name(trace.queue_name);
    added_trace->mutable_frame_enqueue()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_frame_enqueue()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const FrameDequeueH2DTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"core_op_handle", json_to_string(trace.core_op_handle)},
        {"queue_name", json_to_string(trace.queue_name)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_frame_dequeue()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__H2D);
    added_trace->mutable_frame_dequeue()->set_device_id(trace.device_id);
    added_trace->mutable_frame_dequeue()->set_stream_name(trace.queue_name);
    added_trace->mutable_frame_dequeue()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_frame_dequeue()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const FrameDequeueD2HTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"core_op_handle", json_to_string(trace.core_op_handle)},
        {"queue_name", json_to_string(trace.queue_name)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_frame_dequeue()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__D2H);
    added_trace->mutable_frame_dequeue()->set_stream_name(trace.queue_name);
    added_trace->mutable_frame_dequeue()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_frame_dequeue()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const FrameEnqueueD2HTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"core_op_handle", json_to_string(trace.core_op_handle)},
        {"queue_name", json_to_string(trace.queue_name)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_frame_enqueue()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__D2H);
    added_trace->mutable_frame_enqueue()->set_device_id(trace.device_id);
    added_trace->mutable_frame_enqueue()->set_stream_name(trace.queue_name);
    added_trace->mutable_frame_enqueue()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_frame_enqueue()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const SwitchCoreOpTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"core_op_handle", json_to_string(trace.core_op_handle)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_switched_core_op()->set_device_id(trace.device_id);
    added_trace->mutable_switched_core_op()->set_new_core_op_handle(trace.core_op_handle);
    added_trace->mutable_switched_core_op()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const SetCoreOpTimeoutTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"core_op_handle", json_to_string(trace.core_op_handle)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_core_op_set_value()->set_timeout((trace.timeout).count());
    added_trace->mutable_core_op_set_value()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_core_op_set_value()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const SetCoreOpThresholdTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"core_op_handle", json_to_string(trace.core_op_handle)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_core_op_set_value()->set_threshold(trace.threshold);
    added_trace->mutable_core_op_set_value()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_core_op_set_value()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const SetCoreOpPriorityTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"core_op_handle", json_to_string(trace.core_op_handle)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_core_op_set_value()->set_priority(trace.priority);
    added_trace->mutable_core_op_set_value()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_core_op_set_value()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const OracleDecisionTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"reason", json_to_string(trace.reason_idle)},
        {"core_op_handle", json_to_string(trace.core_op_handle)}
    }));

    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_switch_core_op_decision()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_switch_core_op_decision()->set_time_stamp(trace.timestamp);
    added_trace->mutable_switch_core_op_decision()->set_over_threshold(trace.over_threshold);
    added_trace->mutable_switch_core_op_decision()->set_switch_because_idle(trace.reason_idle);
    added_trace->mutable_switch_core_op_decision()->set_over_timeout(trace.over_timeout);
}

void SchedulerProfilerHandler::handle_trace(const DumpProfilerStateTrace &trace)
{
    (void)trace;
    serialize_and_dump_proto();
    m_profiler_trace_proto.Clear();
}

}