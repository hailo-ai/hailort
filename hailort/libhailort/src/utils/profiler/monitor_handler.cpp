/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file monitor_handler.cpp
 * @brief Implementation of the scheduler monitor handlers base with HailoRT tracer mechanism
 **/

#include "monitor_handler.hpp"

#include "common/logger_macros.hpp"
#include "common/os_utils.hpp"

namespace hailort
{
MonitorHandler::MonitorHandler()
{}

MonitorHandler::~MonitorHandler()
{
    clear_monitor();
}

void MonitorHandler::clear_monitor() {

    if (m_is_monitor_currently_working) {
        m_is_monitor_currently_working = false;
        m_mon_shutdown_event->signal();
        if (m_mon_thread.joinable()) {
            m_mon_thread.join();
        }
    }
    m_devices_info.clear();
    m_core_ops_info.clear();
}

void MonitorHandler::handle_trace(const SchedulerStartTrace &trace)
{
    m_device_count = trace.device_count;
    start_mon();
}

void MonitorHandler::handle_trace(const CoreOpIdleTrace &trace)
{
    update_utilization_read_buffers_finished(trace.device_id, trace.core_op_handle, true);
}

void MonitorHandler::handle_trace(const AddCoreOpTrace &trace)
{
    m_core_ops_info[trace.core_op_handle].utilization = 0;
    m_core_ops_info[trace.core_op_handle].core_op_name = trace.core_op_name;
    m_core_ops_info[trace.core_op_handle].is_nms = trace.is_nms;
}

void MonitorHandler::handle_trace(const AddDeviceTrace &trace)
{
    DeviceInfo device_info(trace.device_id, trace.device_arch);
    m_devices_info.emplace(trace.device_id, device_info); 
}

void MonitorHandler::handle_trace(const SwitchCoreOpTrace &trace)
{
    assert(contains(m_devices_info, trace.device_id));
    m_devices_info.at(trace.device_id).current_core_op_handle = trace.core_op_handle;
}

void MonitorHandler::handle_trace(const CreateCoreOpInputStreamsTrace &trace)
{
    // TODO- HRT-10371 'if' should be removed, this is temporary solution since this trace is called out of the scheduler.
    if (!m_is_monitor_currently_working) { return; }
    auto core_op_handle = get_core_op_handle_by_name(trace.core_op_name);
    assert(contains(m_core_ops_info, core_op_handle));
    m_core_ops_info[core_op_handle].input_streams_info[trace.stream_name] = StreamsInfo{trace.queue_size, 0};
}

void MonitorHandler::handle_trace(const CreateCoreOpOutputStreamsTrace &trace)
{
    // TODO- HRT-10371 'if' should be removed, this is temporary solution since this trace is called out of the scheduler.
    if (!m_is_monitor_currently_working) { return; }
    auto core_op_handle = get_core_op_handle_by_name(trace.core_op_name);
    assert(contains(m_core_ops_info, core_op_handle));
    m_core_ops_info[core_op_handle].output_streams_info[trace.stream_name] = StreamsInfo{trace.queue_size, 0};
}

void MonitorHandler::handle_trace(const WriteFrameTrace &trace)
{
    assert(contains(m_core_ops_info, trace.core_op_handle));
    assert(contains(m_core_ops_info[trace.core_op_handle].input_streams_info, trace.queue_name));
    m_core_ops_info[trace.core_op_handle].input_streams_info[trace.queue_name].pending_frames_count++;
}

void MonitorHandler::handle_trace(const ReadFrameTrace &trace)
{
    assert(contains(m_core_ops_info, trace.core_op_handle));
    assert(contains(m_core_ops_info[trace.core_op_handle].output_streams_info, trace.queue_name));
    m_core_ops_info[trace.core_op_handle].output_streams_info[trace.queue_name].pending_frames_count--;
    m_core_ops_info[trace.core_op_handle].output_streams_info[trace.queue_name].total_frames_count++;
}

void MonitorHandler::handle_trace(const OutputVdmaEnqueueTrace &trace)
{
    assert(contains(m_core_ops_info, trace.core_op_handle));
    assert(contains(m_core_ops_info[trace.core_op_handle].output_streams_info, trace.queue_name));
    m_core_ops_info[trace.core_op_handle].output_streams_info[trace.queue_name].pending_frames_count += trace.frames;
}

void MonitorHandler::handle_trace(const InputVdmaDequeueTrace &trace)
{
    assert(contains(m_core_ops_info, trace.core_op_handle));
    assert(contains(m_core_ops_info[trace.core_op_handle].input_streams_info, trace.queue_name));
    m_core_ops_info[trace.core_op_handle].input_streams_info[trace.queue_name].pending_frames_count--;
    update_utilization_send_started(trace.device_id);
}

scheduler_core_op_handle_t MonitorHandler::get_core_op_handle_by_name(const std::string &name)
{
    for (const auto &core_op_info : m_core_ops_info) {
        if (0 == core_op_info.second.core_op_name.compare(name)) {
            return core_op_info.first;
        }
    }
    return INVALID_CORE_OP_HANDLE;
}

hailo_status MonitorHandler::start_mon()
{
#if defined(__GNUC__)

    /* Clearing monitor members. Since the owner of monitor_handler is tracer, which is static, 
    the monitor may get rerun without destructor being called. */
    if (m_is_monitor_currently_working) {
        clear_monitor();
    }
    m_is_monitor_currently_working = true;

    m_mon_shutdown_event = Event::create_shared(Event::State::not_signalled);
    m_last_measured_timestamp = std::chrono::steady_clock::now();
    CHECK(nullptr != m_mon_shutdown_event, HAILO_OUT_OF_HOST_MEMORY);

    auto tmp_file = open_temp_mon_file();
    CHECK_EXPECTED_AS_STATUS(tmp_file);
    m_mon_tmp_output = tmp_file.release();

    m_mon_thread = std::thread([this] ()
    {
        while (true) {
            auto status = m_mon_shutdown_event->wait(DEFAULT_SCHEDULER_MON_INTERVAL);
            if (HAILO_TIMEOUT == status) {
                dump_state();
            } else if (HAILO_SUCCESS == status) {
                break; // shutdown_event was signaled
            } else if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Scheduler monitor failed with status {}", status);
                return;
            }
        }
        return;
    });

    return HAILO_SUCCESS;
#else
    return HAILO_NOT_IMPLEMENTED;
#endif
}

std::string get_curr_pid_as_str()
{
    return std::to_string(OsUtils::get_curr_pid());
}

#if defined(__GNUC__)
Expected<std::shared_ptr<TempFile>> MonitorHandler::open_temp_mon_file()
{
    std::string file_name = get_curr_pid_as_str();
    auto tmp_file = TempFile::create(file_name, SCHEDULER_MON_TMP_DIR);
    CHECK_EXPECTED(tmp_file);
    
    auto tmp_file_ptr = make_shared_nothrow<TempFile>(tmp_file.release());
    CHECK_AS_EXPECTED(nullptr != tmp_file_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return tmp_file_ptr;
}

void MonitorHandler::dump_state()
{
    auto file = LockedFile::create(m_mon_tmp_output->name(), "w");
    if (HAILO_SUCCESS != file.status()) {
        LOGGER__ERROR("Failed to open and lock file {}, with status: {}", m_mon_tmp_output->name(), file.status());
        return;
    }

    ProtoMon mon;
    mon.set_pid(get_curr_pid_as_str());
    time_dependent_events_cycle_calc();
    log_monitor_networks_infos(mon);
    log_monitor_device_infos(mon);
    log_monitor_frames_infos(mon);

    clear_accumulators();

    if (!mon.SerializeToFileDescriptor(file->get_fd())) {
        LOGGER__ERROR("Failed to SerializeToFileDescriptor(), with errno: {}", errno);
    }
}
#endif

void MonitorHandler::time_dependent_events_cycle_calc()
{
    auto curr_time = std::chrono::steady_clock::now();
    m_last_measured_time_duration = std::chrono::duration_cast<std::chrono::duration<double>>(curr_time - m_last_measured_timestamp).count();

    for (auto &device : m_devices_info) {
        if (!device.second.device_has_drained_everything) {
            update_utilization_read_buffers_finished(device.second.device_id, device.second.current_core_op_handle, false);
        }
    }
    m_last_measured_timestamp = curr_time;
}

void MonitorHandler::log_monitor_device_infos(ProtoMon &mon)
{
    for (auto const &device_info_pair : m_devices_info) {
        auto device_info = device_info_pair.second;
        auto curr_device_utilization = device_info.device_utilization_duration;
        auto utilization_percentage = ((curr_device_utilization * 100) /  m_last_measured_time_duration);

        auto device_infos = mon.add_device_infos();
        device_infos->set_device_id(device_info.device_id);
        device_infos->set_utilization(utilization_percentage);
        device_infos->set_device_arch(device_info.device_arch);
    }
}

void MonitorHandler::log_monitor_networks_infos(ProtoMon &mon)
{
    for (uint32_t core_op_handle = 0; core_op_handle < m_core_ops_info.size(); core_op_handle++) {
        auto curr_core_op_utilization = m_core_ops_info[core_op_handle].utilization;
        auto utilization = ((curr_core_op_utilization * 100) /  m_last_measured_time_duration);
        double min_fps = std::numeric_limits<double>::max();

        for (auto const &stream : m_core_ops_info[core_op_handle].output_streams_info) {
            double fps = stream.second.total_frames_count / m_last_measured_time_duration;
            min_fps = (fps < min_fps) ? fps : min_fps;
        }

        auto net_info = mon.add_networks_infos();
        net_info->set_network_name(m_core_ops_info[core_op_handle].core_op_name);
        net_info->set_utilization(utilization);
        net_info->set_fps(min_fps);
    }
}
 
void MonitorHandler::log_monitor_frames_infos(ProtoMon &mon)
{
    for (uint32_t core_op_handle = 0; core_op_handle < m_core_ops_info.size(); core_op_handle++) {
        assert(contains(m_core_ops_info, core_op_handle));
        auto net_frames_info = mon.add_net_frames_infos();
        for (auto const &stream : m_core_ops_info[core_op_handle].input_streams_info) {
            net_frames_info->set_network_name(m_core_ops_info[core_op_handle].core_op_name);
            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream.first);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__HOST_TO_DEVICE);
            stream_frames_info->set_buffer_frames_size(static_cast<int32_t>(stream.second.queue_size * m_device_count));
            stream_frames_info->set_pending_frames_count(static_cast<int32_t>(stream.second.pending_frames_count));
        }
        
        for (auto const &stream : m_core_ops_info[core_op_handle].output_streams_info) {
            net_frames_info->set_network_name(m_core_ops_info[core_op_handle].core_op_name);
            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream.first);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__DEVICE_TO_HOST);
            if (m_core_ops_info[core_op_handle].is_nms) {
                stream_frames_info->set_pending_frames_count(SCHEDULER_MON_NAN_VAL);
                stream_frames_info->set_buffer_frames_size(SCHEDULER_MON_NAN_VAL);
            } else {
                stream_frames_info->set_pending_frames_count(static_cast<int32_t>(stream.second.pending_frames_count));
                stream_frames_info->set_buffer_frames_size(static_cast<int32_t>(stream.second.queue_size * m_device_count));
            }
        }
    }
}

void MonitorHandler::update_utilization_timers(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle)
{
    assert(contains(m_core_ops_info, core_op_handle));
    assert(contains(m_devices_info, device_id));

    auto time_diff = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::steady_clock::now() - m_devices_info.at(device_id).last_measured_utilization_timestamp).count();

    m_devices_info.at(device_id).device_utilization_duration += time_diff;
    m_core_ops_info[core_op_handle].utilization += time_diff;
}

void MonitorHandler::update_utilization_timestamp(const device_id_t &device_id)
{
    assert(contains(m_devices_info, device_id));
    m_devices_info.at(device_id).last_measured_utilization_timestamp = std::chrono::steady_clock::now();
}

void MonitorHandler::update_utilization_send_started(const device_id_t &device_id)
{
    assert(contains(m_devices_info, device_id));
    if (m_devices_info.at(device_id).device_has_drained_everything) {
        update_device_drained_state(device_id, false);
        update_utilization_timestamp(device_id);
    }
}

void MonitorHandler::update_device_drained_state(const device_id_t &device_id, bool state)
{
    assert(contains(m_devices_info, device_id));
    m_devices_info.at(device_id).device_has_drained_everything = state;
}

void MonitorHandler::update_utilization_read_buffers_finished(const device_id_t &device_id, 
    scheduler_core_op_handle_t core_op_handle, bool is_drained_everything)
{
    update_utilization_timers(device_id, core_op_handle);
    update_device_drained_state(device_id, is_drained_everything);
    if (!is_drained_everything) {
        update_utilization_timestamp(device_id);
    }
}

void MonitorHandler::clear_accumulators()
{
    for (auto &device_info : m_devices_info) {
        device_info.second.device_utilization_duration = 0;
    }

    for (auto &handle_core_op_pair : m_core_ops_info) {
        for (auto &handle_streams_pair : handle_core_op_pair.second.output_streams_info) {
            handle_streams_pair.second.total_frames_count = 0;
        }
        handle_core_op_pair.second.utilization = 0;
    }
}

}