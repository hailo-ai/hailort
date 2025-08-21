/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler.hpp
 * @brief Class declaration for CoreOpsScheduler that schedules core-ops to be active depending on the scheduling algorithm.
 **/

#ifndef _HAILO_SCHEDULED_CORE_OP_HPP_
#define _HAILO_SCHEDULED_CORE_OP_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/utils.hpp"

#include "core_op/core_op.hpp"

#include <condition_variable>
#include <queue>


namespace hailort
{

constexpr const char *INVALID_DEVICE_ID = "";

using core_op_priority_t = uint8_t;

constexpr const uint16_t SINGLE_CONTEXT_BATCH_SIZE = 1;

class VDeviceCoreOp;
class VdmaConfigCoreOp;

using StreamInfoVector = std::vector<hailo_stream_info_t>;


class ScheduledCoreOp
{
public:
    static Expected<std::shared_ptr<ScheduledCoreOp>> create(std::shared_ptr<VDeviceCoreOp> added_core_op,
        StreamInfoVector &stream_infos);

    virtual ~ScheduledCoreOp()  = default;
    ScheduledCoreOp(const ScheduledCoreOp &other) = delete;
    ScheduledCoreOp &operator=(const ScheduledCoreOp &other) = delete;
    ScheduledCoreOp &operator=(ScheduledCoreOp &&other) = delete;
    ScheduledCoreOp(ScheduledCoreOp &&other) noexcept = delete;

    std::shared_ptr<CoreOp> get_core_op();

    Expected<std::shared_ptr<VdmaConfigCoreOp>> get_vdma_core_op(const device_id_t &device_id);

    uint32_t get_max_ongoing_frames_per_device() const;

    uint16_t get_max_batch_size() const;
    uint16_t get_burst_size() const;
    bool use_dynamic_batch_flow() const;

    device_id_t get_last_device();
    void set_last_device(const device_id_t &device_id);

    std::chrono::milliseconds get_timeout();
    hailo_status set_timeout(const std::chrono::milliseconds &timeout);
    uint32_t get_threshold();
    hailo_status set_threshold(uint32_t threshold);
    core_op_priority_t get_priority();
    void set_priority(core_op_priority_t priority);

    bool is_over_threshold(uint32_t num_of_frames) const;
    bool is_over_threshold_timeout() const;
    bool is_first_frame() const;

    std::chrono::time_point<std::chrono::steady_clock> get_last_run_timestamp();
    void set_last_run_timestamp(const std::chrono::time_point<std::chrono::steady_clock> &timestamp);

    std::atomic_uint32_t &get_num_pending_requests() { return m_pending_requests; }
    std::atomic_uint32_t &get_num_ready_requests() { return m_ready_requests; }

    void add_instance();
    void remove_instance();
    size_t instances_count() const;

    ScheduledCoreOp(std::shared_ptr<VDeviceCoreOp> core_op, std::chrono::milliseconds timeout,
        uint16_t max_batch_size,  uint32_t max_ongoing_frames_per_device, bool use_dynamic_batch_flow);

private:
    std::shared_ptr<VDeviceCoreOp> m_core_op;
    std::chrono::time_point<std::chrono::steady_clock> m_last_run_time_stamp;
    std::chrono::milliseconds m_timeout;
    const uint16_t m_max_batch_size;
    const uint32_t m_max_ongoing_frames_per_device;
    const bool m_use_dynamic_batch_flow;
    size_t m_instances_count;

    std::atomic_uint32_t m_pending_requests;
    std::atomic_uint32_t m_ready_requests;
    uint32_t m_min_threshold;

    core_op_priority_t m_priority;

    device_id_t m_last_device_id;
};


using ScheduledCoreOpPtr = std::shared_ptr<ScheduledCoreOp>;

} /* namespace hailort */

#endif /* _HAILO_SCHEDULED_CORE_OP_HPP_ */
