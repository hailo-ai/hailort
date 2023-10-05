/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file monitor_handler.hpp
 * @brief Implementation of the scheduler monitor handlers base with HailoRT tracer mechanism
 **/

#ifndef _HAILO_MONITOR_HANDLER_HPP_
#define _HAILO_MONITOR_HANDLER_HPP_

#include "handler.hpp"

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/event.hpp"

#include "common/filesystem.hpp"
#include "common/utils.hpp"
#include "common/runtime_statistics_internal.hpp"

#include "vdevice/scheduler/scheduler_base.hpp"

#include <iostream>
#include <string>
#include <thread>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "scheduler_mon.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop )
#else
#pragma GCC diagnostic pop
#endif

namespace hailort
{

#define SCHEDULER_MON_TMP_DIR ("/tmp/hmon_files/")
#define SCHEDULER_MON_ENV_VAR ("HAILO_MONITOR")
#define SCHEDULER_MON_ENV_VAR_VALUE ("1")
#define DEFAULT_SCHEDULER_MON_INTERVAL (std::chrono::seconds(1))
#define SCHEDULER_MON_NAN_VAL (-1)

using stream_name = std::string;

struct DeviceInfo {
    DeviceInfo(const device_id_t &device_id, const std::string &device_arch) :
        device_id(device_id), device_arch(device_arch), device_has_drained_everything(true),
        device_utilization_duration(0), last_measured_utilization_timestamp(std::chrono::steady_clock::now()),
        current_core_op_handle(INVALID_CORE_OP_HANDLE), requested_transferred_frames_h2d(), finished_transferred_frames_d2h()
    {}
    std::string device_id;
    std::string device_arch;
    bool device_has_drained_everything;
    double device_utilization_duration;
    std::chrono::time_point<std::chrono::steady_clock> last_measured_utilization_timestamp;
    scheduler_core_op_handle_t current_core_op_handle;
    std::unordered_map<scheduler_core_op_handle_t, std::shared_ptr<SchedulerCounter>> requested_transferred_frames_h2d;
    std::unordered_map<scheduler_core_op_handle_t, std::shared_ptr<SchedulerCounter>> finished_transferred_frames_d2h;
};

struct StreamsInfo {
    uint32_t queue_size;
    std::shared_ptr<FullAccumulator<double>> pending_frames_count_acc = make_shared_nothrow<FullAccumulator<double>>("frames_acc");
    std::shared_ptr<std::atomic_uint32_t> pending_frames_count = make_shared_nothrow<std::atomic_uint32_t>(0);
    std::shared_ptr<std::atomic_uint32_t> total_frames_count = make_shared_nothrow<std::atomic_uint32_t>(0);
};

struct CoreOpInfo {
    std::unordered_map<stream_name, StreamsInfo> input_streams_info;
    std::unordered_map<stream_name, StreamsInfo> output_streams_info;
    std::string core_op_name;
    bool is_nms;
    double utilization;
};

class MonitorHandler : public Handler
{
public:
    MonitorHandler(MonitorHandler const&) = delete;
    void operator=(MonitorHandler const&) = delete;

    MonitorHandler();
    ~MonitorHandler();
    void clear_monitor();

    virtual void handle_trace(const AddCoreOpTrace&) override;
    virtual void handle_trace(const CreateCoreOpInputStreamsTrace&) override;
    virtual void handle_trace(const CreateCoreOpOutputStreamsTrace&) override;
    virtual void handle_trace(const WriteFrameTrace&) override;
    virtual void handle_trace(const ReadFrameTrace&) override;
    virtual void handle_trace(const InputVdmaDequeueTrace&) override;
    virtual void handle_trace(const OutputVdmaEnqueueTrace&) override;
    virtual void handle_trace(const SwitchCoreOpTrace&) override;
    virtual void handle_trace(const MonitorStartTrace&) override;
    virtual void handle_trace(const AddDeviceTrace&) override;

private:
    hailo_status start_mon();
#if defined(__GNUC__)
    Expected<std::shared_ptr<TempFile>> open_temp_mon_file();
    void dump_state();
#endif
    void time_dependent_events_cycle_calc();
    void log_monitor_device_infos(ProtoMon &mon);
    void log_monitor_networks_infos(ProtoMon &mon);
    void log_monitor_frames_infos(ProtoMon &mon);
    void update_utilization_timers(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle);
    void update_utilization_timestamp(const device_id_t &device_id);
    void update_utilization_send_started(const device_id_t &device_id);
    void update_device_drained_state(const device_id_t &device_id, bool state);
    void update_utilization_read_buffers_finished(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle, bool is_drained_everything);
    void clear_accumulators();
    scheduler_core_op_handle_t get_core_op_handle_by_name(const std::string &name);

    bool m_is_monitor_currently_working = false;
    uint32_t m_device_count;
    std::thread m_mon_thread;
    EventPtr m_mon_shutdown_event;
#if defined(__GNUC__)
    std::shared_ptr<TempFile> m_mon_tmp_output;
#endif
    std::chrono::time_point<std::chrono::steady_clock> m_last_measured_timestamp;
    double m_last_measured_time_duration;
    // TODO: Consider adding Accumulator classes for more info (min, max, mean, etc..)
    std::unordered_map<scheduler_core_op_handle_t, CoreOpInfo> m_core_ops_info;
    std::unordered_map<device_id_t, DeviceInfo> m_devices_info;
};
}

#endif /* _MONITOR_HANDLER_HPP_ */