/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
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

void MonitorHandler::clear_monitor()
{
    m_unique_vdevice_hash = {};
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

void MonitorHandler::handle_trace(const MonitorStartTrace &trace)
{
    start_mon(trace.unique_vdevice_hash);
}

void MonitorHandler::handle_trace(const MonitorEndTrace &trace)
{
    if (m_unique_vdevice_hash == trace.unique_vdevice_hash) {
        clear_monitor();
    }
}

void MonitorHandler::handle_trace(const AddCoreOpTrace &trace)
{
    m_core_ops_info[trace.core_op_handle].utilization = 0;
    m_core_ops_info[trace.core_op_handle].core_op_name = trace.core_op_name;
}

void MonitorHandler::handle_trace(const AddDeviceTrace &trace)
{
    DeviceInfo device_info(trace.device_id, trace.device_arch);
    m_devices_info.emplace(trace.device_id, device_info);
}

void MonitorHandler::handle_trace(const ActivateCoreOpTrace &trace)
{
    // TODO: 'if' should be removed, this is temporary solution since this trace is called out of the scheduler or vdevice.
    if (!m_is_monitor_currently_working) { return; }
    if (!contains(m_devices_info, trace.device_id)) { return; } // TODO (HRT-8835): Support multiple vdevices
    m_devices_info.at(trace.device_id).current_core_op_handle = trace.core_op_handle;
}

void MonitorHandler::handle_trace(const AddStreamH2DTrace &trace)
{
    auto core_op_handle = get_core_op_handle_by_name(trace.core_op_name);
    if (!contains(m_core_ops_info, core_op_handle)) { return; } // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_devices_info, trace.device_id)) { return; } // TODO (HRT-8835): Support multiple vdevices
    m_core_ops_info[core_op_handle].input_streams_info[trace.stream_name] = StreamsInfo{trace.queue_size};
    if (!contains(m_devices_info.at(trace.device_id).requested_transferred_frames_h2d, core_op_handle)) {
        m_devices_info.at(trace.device_id).requested_transferred_frames_h2d.emplace(core_op_handle, make_shared_nothrow<SchedulerCounter>());
    }
    m_devices_info.at(trace.device_id).requested_transferred_frames_h2d[core_op_handle]->insert(trace.stream_name);
}

void MonitorHandler::handle_trace(const AddStreamD2HTrace &trace)
{
    auto core_op_handle = get_core_op_handle_by_name(trace.core_op_name);
    if (!contains(m_core_ops_info, core_op_handle)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_devices_info, trace.device_id)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    m_core_ops_info[core_op_handle].output_streams_info[trace.stream_name] = StreamsInfo{trace.queue_size};
    if (!contains(m_devices_info.at(trace.device_id).finished_transferred_frames_d2h, core_op_handle)) {
        m_devices_info.at(trace.device_id).finished_transferred_frames_d2h.emplace(core_op_handle, make_shared_nothrow<SchedulerCounter>());
    }
    m_devices_info.at(trace.device_id).finished_transferred_frames_d2h[core_op_handle]->insert(trace.stream_name);
}

void MonitorHandler::handle_trace(const FrameEnqueueH2DTrace &trace)
{
    if (!contains(m_core_ops_info, trace.core_op_handle)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_core_ops_info[trace.core_op_handle].input_streams_info, trace.queue_name)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    auto &queue = m_core_ops_info[trace.core_op_handle].input_streams_info[trace.queue_name];
    queue.pending_frames_count->fetch_add(1);
    queue.pending_frames_count_acc->add_data_point(queue.pending_frames_count->load());
}

void MonitorHandler::handle_trace(const FrameDequeueD2HTrace &trace)
{
    if (!contains(m_core_ops_info, trace.core_op_handle)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_core_ops_info[trace.core_op_handle].output_streams_info, trace.queue_name)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    auto &queue = m_core_ops_info[trace.core_op_handle].output_streams_info[trace.queue_name];
    queue.pending_frames_count->fetch_sub(1);
    queue.pending_frames_count_acc->add_data_point(queue.pending_frames_count->load());
    queue.total_frames_count->fetch_add(1);
}

void MonitorHandler::handle_trace(const FrameEnqueueD2HTrace &trace)
{
    // TODO: 'if' should be removed, this is temporary solution since this trace is called out of the scheduler or vdevice.
    if (!m_is_monitor_currently_working) { return; }
    if (!contains(m_core_ops_info, trace.core_op_handle)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_core_ops_info[trace.core_op_handle].output_streams_info, trace.queue_name)) { return ;} // TODO (HRT-8835): Support multiple vdevices

    if (!contains(m_devices_info, trace.device_id)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_devices_info.at(trace.device_id).requested_transferred_frames_h2d, trace.core_op_handle)) { return ;} // TODO (HRT-8835): Support multiple vdevices

    auto &queue = m_core_ops_info[trace.core_op_handle].output_streams_info[trace.queue_name];
    queue.pending_frames_count->fetch_add(1);
    queue.pending_frames_count_acc->add_data_point(queue.pending_frames_count->load());

    m_devices_info.at(trace.device_id).finished_transferred_frames_d2h[trace.core_op_handle]->increase(trace.queue_name);

    const auto max_transferred_h2d = m_devices_info.at(trace.device_id).requested_transferred_frames_h2d[trace.core_op_handle]->get_max_value();
    const auto min_transferred_d2h = m_devices_info.at(trace.device_id).finished_transferred_frames_d2h[trace.core_op_handle]->get_min_value();
    if(max_transferred_h2d == min_transferred_d2h) {
            update_utilization_read_buffers_finished(trace.device_id, trace.core_op_handle, true);
    }
}

void MonitorHandler::handle_trace(const FrameDequeueH2DTrace &trace)
{
    // TODO: 'if' should be removed, this is temporary solution since this trace is called out of the scheduler or vdevice.
    if (!m_is_monitor_currently_working) { return; }
    if (!contains(m_core_ops_info, trace.core_op_handle)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_core_ops_info[trace.core_op_handle].input_streams_info, trace.queue_name)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_devices_info, trace.device_id)) { return ;} // TODO (HRT-8835): Support multiple vdevices
    if (!contains(m_devices_info.at(trace.device_id).requested_transferred_frames_h2d, trace.core_op_handle)) { return ;} // TODO (HRT-8835): Support multiple vdevices

    auto &queue = m_core_ops_info[trace.core_op_handle].input_streams_info[trace.queue_name];
    queue.pending_frames_count->fetch_sub(1);
    queue.pending_frames_count_acc->add_data_point(queue.pending_frames_count->load());

    m_devices_info.at(trace.device_id).requested_transferred_frames_h2d[trace.core_op_handle]->increase(trace.queue_name);

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

hailo_status MonitorHandler::start_mon(const std::string &unique_vdevice_hash)
{
#if defined(__GNUC__)

    /* Clearing monitor members. Since the owner of monitor_handler is tracer, which is static,
    the monitor may get rerun without destructor being called. */
    if (m_is_monitor_currently_working) {
        if (!m_unique_vdevice_hash.empty() && (unique_vdevice_hash != m_unique_vdevice_hash)) {
            LOGGER__WARNING("Trying to register a vdevice to hailo-monitor, "\
                "while other vdevice is registered. Monitor currently supports single vdevice, which will result in non-consistent tracing.");
            return HAILO_INVALID_OPERATION;
        }
        clear_monitor();
    }
    m_unique_vdevice_hash = unique_vdevice_hash;
    m_is_monitor_currently_working = true;

    auto event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED_AS_STATUS(event_exp);
    m_mon_shutdown_event = event_exp.release();
    m_last_measured_timestamp = std::chrono::steady_clock::now();

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
    (void)unique_vdevice_hash;
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
        auto curr_device_utilization = device_info_pair.second.device_utilization_duration;
        auto utilization_percentage = ((curr_device_utilization * 100) /  m_last_measured_time_duration);

        auto device_infos = mon.add_device_infos();
        device_infos->set_device_id(device_info_pair.second.device_id);
        device_infos->set_utilization(utilization_percentage);
        device_infos->set_device_arch(device_info_pair.second.device_arch);
    }
}

void MonitorHandler::log_monitor_networks_infos(ProtoMon &mon)
{
    for (uint32_t core_op_handle = 0; core_op_handle < m_core_ops_info.size(); core_op_handle++) {
        auto curr_core_op_utilization = m_core_ops_info[core_op_handle].utilization;
        auto utilization = ((curr_core_op_utilization * 100) /  m_last_measured_time_duration);
        double min_fps = std::numeric_limits<double>::max();

        for (auto const &stream : m_core_ops_info[core_op_handle].output_streams_info) {
            double fps = stream.second.total_frames_count->load() / m_last_measured_time_duration;
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
            stream_frames_info->set_buffer_frames_size(static_cast<int32_t>(stream.second.queue_size * m_devices_info.size()));
            stream_frames_info->set_pending_frames_count(static_cast<int32_t>(stream.second.pending_frames_count->load()));

            auto expected_min_val = stream.second.pending_frames_count_acc->min();
            if (expected_min_val.status() == HAILO_SUCCESS) {
                stream_frames_info->set_min_pending_frames_count(static_cast<int32_t>(expected_min_val.release()));
            } else {
                stream_frames_info->set_min_pending_frames_count(-1);
            }

            auto expected_max_val = stream.second.pending_frames_count_acc->max();
            if (expected_max_val.status() == HAILO_SUCCESS) {
                stream_frames_info->set_max_pending_frames_count(static_cast<int32_t>(expected_max_val.release()));
            } else {
                stream_frames_info->set_max_pending_frames_count(-1);
            }

            auto expected_avg_val = stream.second.pending_frames_count_acc->mean();
            if (expected_avg_val.status() == HAILO_SUCCESS) {
                stream_frames_info->set_avg_pending_frames_count(expected_avg_val.release());
            } else {
                stream_frames_info->set_avg_pending_frames_count(-1);
            }

            stream.second.pending_frames_count_acc->get_and_clear();
        }

        for (auto const &stream : m_core_ops_info[core_op_handle].output_streams_info) {
            net_frames_info->set_network_name(m_core_ops_info[core_op_handle].core_op_name);
            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream.first);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__DEVICE_TO_HOST);

            stream_frames_info->set_pending_frames_count(static_cast<int32_t>(stream.second.pending_frames_count->load()));
            stream_frames_info->set_buffer_frames_size(static_cast<int32_t>(stream.second.queue_size * m_devices_info.size()));

            auto expected_min_val = stream.second.pending_frames_count_acc->min();
            if (expected_min_val.status() == HAILO_SUCCESS) {
                stream_frames_info->set_min_pending_frames_count(static_cast<int32_t>(expected_min_val.release()));
            } else {
                stream_frames_info->set_min_pending_frames_count(-1);
            }

            auto expected_max_val = stream.second.pending_frames_count_acc->max();
            if (expected_max_val.status() == HAILO_SUCCESS) {
                stream_frames_info->set_max_pending_frames_count(static_cast<int32_t>(expected_max_val.release()));
            } else {
                stream_frames_info->set_max_pending_frames_count(-1);
            }

            auto expected_avg_val = stream.second.pending_frames_count_acc->mean();
            if (expected_avg_val.status() == HAILO_SUCCESS) {
                stream_frames_info->set_avg_pending_frames_count(expected_avg_val.release());
            } else {
                stream_frames_info->set_avg_pending_frames_count(-1);
            }

            stream.second.pending_frames_count_acc->get_and_clear();
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
            handle_streams_pair.second.total_frames_count->store(0);
        }
        handle_core_op_pair.second.utilization = 0;
    }
}

}
