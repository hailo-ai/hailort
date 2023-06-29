/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler.hpp
 * @brief Class declaration for CoreOpsScheduler that schedules core-ops to be active depending on the scheduling algorithm.
 **/

#ifndef _HAILO_SCHEDULER_HPP_
#define _HAILO_SCHEDULER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/utils.hpp"
#include "common/filesystem.hpp"

#include "vdevice/scheduler/scheduled_core_op_state.hpp"
#include "vdevice/scheduler/scheduled_core_op_cv.hpp"
#include "vdevice/scheduler/scheduler_base.hpp"


namespace hailort
{

#define INVALID_CORE_OP_HANDLE (UINT32_MAX)

using scheduler_core_op_handle_t = uint32_t;
using core_op_priority_t = uint8_t;

class CoreOpsScheduler;
using CoreOpsSchedulerPtr = std::shared_ptr<CoreOpsScheduler>;

// We use mostly weak pointer for the scheduler to prevent circular dependency of the pointers
using CoreOpsSchedulerWeakPtr = std::weak_ptr<CoreOpsScheduler>;

using stream_name_t = std::string;

class CoreOpsScheduler : public SchedulerBase
{
public:
    static Expected<CoreOpsSchedulerPtr> create_round_robin(std::vector<std::string> &devices_ids, 
        std::vector<std::string> &devices_arch);
    CoreOpsScheduler(hailo_scheduling_algorithm_t algorithm, std::vector<std::string> &devices_ids, 
        std::vector<std::string> &devices_arch);

    virtual ~CoreOpsScheduler();
    CoreOpsScheduler(const CoreOpsScheduler &other) = delete;
    CoreOpsScheduler &operator=(const CoreOpsScheduler &other) = delete;
    CoreOpsScheduler &operator=(CoreOpsScheduler &&other) = delete;
    CoreOpsScheduler(CoreOpsScheduler &&other) noexcept = delete;

    Expected<scheduler_core_op_handle_t> add_core_op(std::shared_ptr<CoreOp> added_core_op);

    hailo_status signal_frame_pending_to_send(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);

    hailo_status wait_for_read(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        const std::chrono::milliseconds &timeout, const std::function<bool()> &predicate);

    hailo_status signal_frame_pending_to_read(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);

    void signal_frame_transferred_d2h(const scheduler_core_op_handle_t &core_op_handle,
        const std::string &stream_name, const device_id_t &device_id);
    hailo_status signal_read_finish(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        const device_id_t &device_id);

    hailo_status enable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);
    hailo_status disable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);

    hailo_status set_timeout(const scheduler_core_op_handle_t &core_op_handle, const std::chrono::milliseconds &timeout, const std::string &network_name);
    hailo_status set_threshold(const scheduler_core_op_handle_t &core_op_handle, uint32_t threshold, const std::string &network_name);
    hailo_status set_priority(const scheduler_core_op_handle_t &core_op_handle, core_op_priority_t priority, const std::string &network_name);

    virtual ReadyInfo is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle, bool check_threshold) override;
    virtual bool has_core_op_drained_everything(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id) override;
    hailo_status flush_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name, const std::chrono::milliseconds &timeout);

    void notify_all();

protected:
    bool choose_next_core_op(const device_id_t &device_id, bool check_threshold);

    std::unordered_map<scheduler_core_op_handle_t, std::map<stream_name_t, std::atomic_bool>> m_should_core_op_stop;

private:
    hailo_status switch_core_op(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id,
        bool keep_nn_config = false);
    // Needs to be called with m_before_read_write_mutex held.
    void signal_read_finish_impl(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        const device_id_t &device_id);

    hailo_status send_all_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id, uint32_t burst_size);
    hailo_status send_pending_buffer(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name, const device_id_t &device_id);

    void decrease_core_op_counters(const scheduler_core_op_handle_t &core_op_handle);
    bool should_core_op_stop(const scheduler_core_op_handle_t &core_op_handle);
    bool core_op_all_streams_aborted(const scheduler_core_op_handle_t &core_op_handle);

    std::string get_core_op_name(const scheduler_core_op_handle_t &core_op_handle);
    bool is_core_op_active(const scheduler_core_op_handle_t &core_op_handle);
    bool is_multi_device();

    hailo_status optimize_streaming_if_enabled(const scheduler_core_op_handle_t &core_op_handle);
    uint16_t get_min_avail_buffers_count(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id);
    uint16_t get_min_avail_output_buffers(const scheduler_core_op_handle_t &core_op_handle);

    void worker_thread_main();

    std::vector<std::shared_ptr<ScheduledCoreOp>> m_scheduled_core_ops;
    std::mutex m_before_read_write_mutex;
    std::unordered_map<scheduler_core_op_handle_t, std::shared_ptr<ScheduledCoreOpCV>> m_core_ops_cvs;

    std::atomic_bool m_is_running;
    std::atomic_bool m_execute_worker_thread;
    std::thread m_scheduler_thread;
    std::condition_variable m_scheduler_cv;
};
} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_HPP_ */
