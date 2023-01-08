/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracer.cpp
 * @brief: Tracing mechanism for HailoRT + FW events
 *
 **/


#include "tracer.hpp"
#include "common/utils.hpp"
#include "hailort_logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/null_sink.h>

#include <iomanip>
#include <sstream>

#define SCHEDULER_PROFILER_NAME ("SchedulerProfiler")
#define SCHEDULER_PROFILER_LOGGER_FILENAME ("scheduler_profiler.json")
#define SCHEDULER_PROFILER_LOGGER_PATTERN ("%v")

#define SCHEDULER_PROFILER_LOGGER_PATH ("SCHEDULER_PROFILER_LOGGER_PATH")

#define PROFILER_ENV_VAR ("HAILO_ENABLE_PROFILER")

namespace hailort
{

Tracer::Tracer()
{
    auto should_trace_env = std::getenv(PROFILER_ENV_VAR);
    m_should_trace = ((nullptr != should_trace_env) && (strnlen(should_trace_env, 2) == 1) && (strncmp(should_trace_env, "1", 1) == 0));
    if (m_should_trace) {
        m_start_time = std::chrono::high_resolution_clock::now();
        int64_t time_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(m_start_time.time_since_epoch()).count();
        m_handlers.push_back(std::make_unique<SchedulerProfilerHandler>(time_since_epoch));
    }
}

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
    ss << "{\"ms_since_epoch_zero_time\": \"" << start_time << "\",\n\"scheduler_actions\": [\n";
    m_profiler_logger->info(ss.str());
#else
    (void)start_time;
#endif
}

SchedulerProfilerHandler::~SchedulerProfilerHandler()
{
    m_profiler_logger->info("]\n}");
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

void SchedulerProfilerHandler::handle_trace(const AddNetworkGroupTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_name", json_to_string(trace.network_group_name)},
        {"network_group_handle", json_to_string(trace.network_group_handle)},
        {"timeout", json_to_string((uint64_t)trace.timeout)},
        {"threshold", json_to_string((uint64_t)trace.threshold)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const CreateNetworkGroupInputStreamsTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_name", json_to_string(trace.network_group_name)},
        {"stream_name", json_to_string(trace.stream_name)},
        {"queue_size", json_to_string(trace.queue_size)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const CreateNetworkGroupOutputStreamsTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_name", json_to_string(trace.network_group_name)},
        {"stream_name", json_to_string(trace.stream_name)},
        {"queue_size", json_to_string(trace.queue_size)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const WriteFrameTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_handle", json_to_string(trace.network_group_handle)},
        {"queue_name", json_to_string(trace.queue_name)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const InputVdmaEnqueueTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_handle", json_to_string(trace.network_group_handle)},
        {"queue_name", json_to_string(trace.queue_name)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const ReadFrameTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_handle", json_to_string(trace.network_group_handle)},
        {"queue_name", json_to_string(trace.queue_name)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const OutputVdmaEnqueueTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_handle", json_to_string(trace.network_group_handle)},
        {"queue_name", json_to_string(trace.queue_name)},
        {"frames", json_to_string(trace.frames)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const ChooseNetworkGroupTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"chosen_network_group_handle", json_to_string(trace.network_group_handle)},
        {"threshold", json_to_string(trace.threshold)},
        {"timeout", json_to_string(trace.timeout)}
    }));
}

void SchedulerProfilerHandler::handle_trace(const SwitchNetworkGroupTrace &trace)
{
    log(JSON({
        {"action", json_to_string(trace.name)},
        {"timestamp", json_to_string(trace.timestamp)},
        {"device_id", json_to_string(trace.device_id)},
        {"network_group_handle", json_to_string(trace.network_group_handle)}
    }));
}


}
