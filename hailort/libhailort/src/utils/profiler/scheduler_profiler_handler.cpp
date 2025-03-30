/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <fstream>
#include <iomanip>
#include <mutex>


static const std::string PROFILER_DEFAULT_FILE_NAME_PREFIX("hailort");
static const std::string PROFILER_DEFAULT_FILE_NAME_SUFFIX(".hrtt");

namespace hailort
{

std::string get_current_datetime() {
  auto now = std::chrono::system_clock::now();
  auto tp = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&tp), "%Y-%m-%d_%H-%M-%S");
  return ss.str();
}

SchedulerProfilerHandler::SchedulerProfilerHandler(size_t dump_after_n_seconds, size_t dump_after_n_kb) :
      m_time_in_seconds_bounded_dump(dump_after_n_seconds),
      m_size_in_kb_bounded_dump(dump_after_n_kb)
{
    if (m_time_in_seconds_bounded_dump) {
        m_timer_thread = std::thread([this](){
            std::unique_lock<std::mutex> lock(m_cv_mutex);
            m_cv.wait_for(lock,
                std::chrono::seconds(m_time_in_seconds_bounded_dump),
                [this] { return m_shutting_down; });
            dump_trace_file();
        });
    }
}

SchedulerProfilerHandler::~SchedulerProfilerHandler()
{
    {
        std::lock_guard<std::mutex> lock(m_cv_mutex);
        m_shutting_down = true;
    }
    m_cv.notify_all();
    if (m_timer_thread.joinable()) {
        m_timer_thread.join();
    }
}

hailo_status SchedulerProfilerHandler::serialize_and_dump_proto()
{
    std::lock_guard<std::mutex> lock(m_dump_file_mutex);

    if(m_file_already_dumped) {
        return HAILO_SUCCESS;
    }

    std::string file_name = PROFILER_DEFAULT_FILE_NAME_PREFIX + "_" + get_current_datetime() + PROFILER_DEFAULT_FILE_NAME_SUFFIX;
    auto file_env_var = get_env_variable(PROFILER_FILE_ENV_VAR);
    if (file_env_var) {
        file_name = file_env_var.value() + PATH_SEPARATOR + file_name;
    }

    std::ofstream output_file(std::string(file_name), std::ios::out |std::ios::binary);
    google::protobuf::io::OstreamOutputStream stream(&output_file);

    if(!m_profiler_trace_proto.SerializeToZeroCopyStream(&stream)) {
        LOGGER__ERROR("Failed writing profiling data to file {}.", file_name);
        return HAILO_FILE_OPERATION_FAILURE;
    } else {
        m_file_already_dumped = true;
        return HAILO_SUCCESS;
    }
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
    if (0 == geteuid()) {
        auto pcie_info = get_pcie_info();
        init->mutable_pcie_info()->set_gen(pcie_info.gen);
        init->mutable_pcie_info()->set_lanes(pcie_info.lanes);
    } else {
        init->mutable_pcie_info()->set_gen("Failed fetching info, root privilege is required");
        init->mutable_pcie_info()->set_lanes("Failed fetching info, root privilege is required");
    }
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

void SchedulerProfilerHandler::handle_trace(const HefLoadedTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);

    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_loaded_hef()->set_hef_md5(reinterpret_cast<const char*>(trace.md5_hash));
    added_trace->mutable_loaded_hef()->set_hef_name(trace.hef_name);
    added_trace->mutable_loaded_hef()->set_dfc_version(trace.dfc_version);
    added_trace->mutable_loaded_hef()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const AddCoreOpTrace &trace)
{
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
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_frame_enqueue()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__H2D);
    added_trace->mutable_frame_enqueue()->set_stream_name(trace.queue_name);
    added_trace->mutable_frame_enqueue()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_frame_enqueue()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const FrameDequeueH2DTrace &trace)
{
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
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_frame_dequeue()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__D2H);
    added_trace->mutable_frame_dequeue()->set_stream_name(trace.queue_name);
    added_trace->mutable_frame_dequeue()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_frame_dequeue()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const FrameEnqueueD2HTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_frame_enqueue()->set_direction(ProtoProfilerStreamDirection::PROTO__STREAM_DIRECTION__D2H);
    added_trace->mutable_frame_enqueue()->set_device_id(trace.device_id);
    added_trace->mutable_frame_enqueue()->set_stream_name(trace.queue_name);
    added_trace->mutable_frame_enqueue()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_frame_enqueue()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const ActivateCoreOpTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_activate_core_op()->set_device_id(trace.device_id);
    added_trace->mutable_activate_core_op()->set_new_core_op_handle(trace.core_op_handle);
    added_trace->mutable_activate_core_op()->set_time_stamp(trace.timestamp);
    added_trace->mutable_activate_core_op()->set_duration(trace.duration);
    added_trace->mutable_activate_core_op()->set_dynamic_batch_size(trace.dynamic_batch_size);
}

void SchedulerProfilerHandler::handle_trace(const DeactivateCoreOpTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_deactivate_core_op()->set_device_id(trace.device_id);
    added_trace->mutable_deactivate_core_op()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_deactivate_core_op()->set_time_stamp(trace.timestamp);
    added_trace->mutable_deactivate_core_op()->set_duration(trace.duration);
}

void SchedulerProfilerHandler::handle_trace(const SetCoreOpTimeoutTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_core_op_set_value()->set_timeout((trace.timeout).count());
    added_trace->mutable_core_op_set_value()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_core_op_set_value()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const SetCoreOpThresholdTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_core_op_set_value()->set_threshold(trace.threshold);
    added_trace->mutable_core_op_set_value()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_core_op_set_value()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const SetCoreOpPriorityTrace &trace)
{
    std::lock_guard<std::mutex> lock(m_proto_lock);
    auto added_trace = m_profiler_trace_proto.add_added_trace();
    added_trace->mutable_core_op_set_value()->set_priority(trace.priority);
    added_trace->mutable_core_op_set_value()->set_core_op_handle(trace.core_op_handle);
    added_trace->mutable_core_op_set_value()->set_time_stamp(trace.timestamp);
}

void SchedulerProfilerHandler::handle_trace(const OracleDecisionTrace &trace)
{
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

bool SchedulerProfilerHandler::should_dump_trace_file()
{
    // Should dump only when the profiler is size bounded or time bounded.
    // If size bounded, the trace proto file size should be big enough.
    // If time bounded, there is a separate thread that will dump the file, so no need to dump here.
    auto is_size_bounded = m_size_in_kb_bounded_dump != 0;
    auto proto_file_size_in_kb = (m_profiler_trace_proto.ByteSizeLong() / 1024);
    return is_size_bounded && (proto_file_size_in_kb >= m_size_in_kb_bounded_dump);
}

}
