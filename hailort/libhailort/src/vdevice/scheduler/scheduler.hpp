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

#include "utils/thread_safe_map.hpp"

#include "vdevice/scheduler/scheduled_core_op_state.hpp"
#include "vdevice/scheduler/scheduler_base.hpp"


namespace hailort
{

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

    hailo_status add_core_op(scheduler_core_op_handle_t core_op_handle, std::shared_ptr<CoreOp> added_core_op);

    // Shutdown the scheduler, stops interrupt thread and deactivate all core ops from all devices. This operation
    // is not recoverable.
    void shutdown();

    hailo_status signal_frame_pending(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
        hailo_stream_direction_t direction);

    void signal_frame_transferred(const scheduler_core_op_handle_t &core_op_handle,
        const std::string &stream_name, const device_id_t &device_id, hailo_stream_direction_t direction);

    void enable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);
    void disable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name);

    hailo_status set_timeout(const scheduler_core_op_handle_t &core_op_handle, const std::chrono::milliseconds &timeout, const std::string &network_name);
    hailo_status set_threshold(const scheduler_core_op_handle_t &core_op_handle, uint32_t threshold, const std::string &network_name);
    hailo_status set_priority(const scheduler_core_op_handle_t &core_op_handle, core_op_priority_t priority, const std::string &network_name);

    virtual ReadyInfo is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle, bool check_threshold,
        const device_id_t &device_id) override;
    virtual bool is_device_idle(const device_id_t &device_id) override;

private:
    hailo_status switch_core_op(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id);

    hailo_status send_all_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id, uint32_t burst_size);

    bool should_core_op_stop(const scheduler_core_op_handle_t &core_op_handle);

    hailo_status optimize_streaming_if_enabled(const scheduler_core_op_handle_t &core_op_handle);

    uint16_t get_frames_ready_to_transfer(scheduler_core_op_handle_t core_op_handle, const device_id_t &device_id) const;

    void schedule();

    class SchedulerThread final {
    public:
        SchedulerThread(CoreOpsScheduler &scheduler);

        ~SchedulerThread();

        SchedulerThread(const SchedulerThread &) = delete;
        SchedulerThread &operator=(const SchedulerThread &) = delete;

        void signal();
        void stop();

    private:
        void worker_thread_main();

        CoreOpsScheduler &m_scheduler;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic_bool m_is_running;
        std::atomic_bool m_execute_worker_thread;
        std::thread m_thread;
    };

    ThreadSafeMap<vdevice_core_op_handle_t, ScheduledCoreOpPtr> m_scheduled_core_ops;

    // This shared mutex guards accessing the scheduler data structures including:
    //   - m_scheduled_core_ops
    //   - m_core_op_priority
    //   - m_next_core_op
    // Any function that is modifing these structures (for example by adding/removing items) must lock this mutex using
    // unique_lock. Any function accessing these structures (for example access to
    // m_scheduled_core_ops.at(core_op_handle) can use shared_lock.
    std::shared_timed_mutex m_scheduler_mutex;

    SchedulerThread m_scheduler_thread;
};
} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_HPP_ */
