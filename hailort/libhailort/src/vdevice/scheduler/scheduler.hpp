/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "common/thread_safe_queue.hpp"

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

class VDeviceCoreOp;

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

    hailo_status add_core_op(scheduler_core_op_handle_t core_op_handle, std::shared_ptr<VDeviceCoreOp> added_core_op);
    void remove_core_op(scheduler_core_op_handle_t core_op_handle);

    // Shutdown the scheduler, stops interrupt thread and deactivate all core ops from all devices. This operation
    // is not recoverable.
    void shutdown();

    hailo_status enqueue_infer_request(const scheduler_core_op_handle_t &core_op_handle, InferRequest &&infer_request);

    hailo_status set_timeout(const scheduler_core_op_handle_t &core_op_handle, const std::chrono::milliseconds &timeout, const std::string &network_name);
    hailo_status set_threshold(const scheduler_core_op_handle_t &core_op_handle, uint32_t threshold, const std::string &network_name);
    hailo_status set_priority(const scheduler_core_op_handle_t &core_op_handle, core_op_priority_t priority, const std::string &network_name);

    virtual ReadyInfo is_core_op_ready_for_run(const scheduler_core_op_handle_t &core_op_handle, bool check_threshold,
        const device_id_t &device_id, bool use_ready_queue) override;
    virtual bool is_core_op_ready_for_prepare(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id) override;
    virtual scheduler_core_op_handle_t get_current_core_op_preparing() const override { return m_current_core_op_preparing; }

private:
    hailo_status switch_core_op(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id);
    hailo_status deactivate_core_op(const device_id_t &device_id);

    hailo_status send_all_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id, uint32_t burst_size);
    hailo_status infer_async(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id);

    hailo_status optimize_streaming_if_enabled(const scheduler_core_op_handle_t &core_op_handle);

    Expected<InferRequest> dequeue_infer_request(scheduler_core_op_handle_t core_op_handle);
    uint16_t get_frames_ready_to_transfer(scheduler_core_op_handle_t core_op_handle, const device_id_t &device_id, 
        bool use_ready_queue) const;

    hailo_status prepare_transfers();
    hailo_status cancel_prepared_transfers(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle);

    Expected<std::shared_ptr<VdmaConfigCoreOp>> get_vdma_core_op(scheduler_core_op_handle_t core_op_handle,
        const device_id_t &device_id);

    void shutdown_core_op(scheduler_core_op_handle_t core_op_handle);
    void schedule();

    void update_closest_threshold_timeout();
    std::chrono::milliseconds get_closest_threshold_timeout() const;

    class SchedulerThread final {
    public:
        SchedulerThread(CoreOpsScheduler &scheduler);

        ~SchedulerThread();

        SchedulerThread(const SchedulerThread &) = delete;
        SchedulerThread &operator=(const SchedulerThread &) = delete;

        void signal(bool execute_worker_thread);
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

    class PreparingThread final {
    public:
        PreparingThread(CoreOpsScheduler &scheduler);

        ~PreparingThread();

        PreparingThread(const PreparingThread &) = delete;
        PreparingThread &operator=(const PreparingThread &) = delete;

        void signal();
        void stop();

    private:
        void prepare_worker_thread_main();

        CoreOpsScheduler &m_scheduler;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic_bool m_is_running;
        std::atomic_bool m_execute_worker_thread;
        std::thread m_thread;
    };
    
    std::unordered_map<vdevice_core_op_handle_t, ScheduledCoreOpPtr> m_scheduled_core_ops;

    using InferRequestQueue = SafeQueue<InferRequest>;
    std::unordered_map<vdevice_core_op_handle_t, InferRequestQueue> m_pending_requests;
    std::unordered_map<vdevice_core_op_handle_t, InferRequestQueue> m_ready_requests;

    // This shared mutex guards accessing the scheduler data structures including:
    //   - m_scheduled_core_ops
    //   - m_pending_requests
    //   - m_core_op_to_run_priority
    // Any function that is modifing these structures (for example by adding/removing items) must lock this mutex using
    // unique_lock. Any function accessing these structures (for example access to
    // m_scheduled_core_ops.at(core_op_handle) can use shared_lock.
    std::shared_timed_mutex m_scheduler_mutex;

    std::chrono::steady_clock::time_point m_closest_threshold_timeout;

    SchedulerThread m_scheduler_thread;
    PreparingThread m_preparing_thread;

    std::atomic<scheduler_core_op_handle_t> m_current_core_op_preparing{INVALID_CORE_OP_HANDLE};

    class SchedulerTrace {
    public:
        template<typename SchedulerBegin, typename SchedulerEnd>
        SchedulerTrace(SchedulerBegin &&scheduler_begin, SchedulerEnd &&scheduler_end)
            : m_id(next_trace_id()),
            m_end(std::forward<SchedulerEnd>(scheduler_end))
        {
            scheduler_begin(m_id);
        }
        
        ~SchedulerTrace();

        SchedulerTrace(const SchedulerTrace&) = delete;
        SchedulerTrace& operator=(const SchedulerTrace&) = delete;

        SchedulerTrace(SchedulerTrace&& other) noexcept;
        SchedulerTrace& operator=(SchedulerTrace&& other) noexcept;


    private:
        static uint64_t next_trace_id();

        uint64_t m_id;
        std::function<void(uint64_t)> m_end;
    };   

};
} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_HPP_ */
