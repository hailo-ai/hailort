/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "hailo/network_rate_calculator.hpp"

#include "common/utils.hpp"

#include "core_op/core_op.hpp"

#include "vdevice/scheduler/scheduler_counter.hpp"

#include <condition_variable>
#include <queue>


namespace hailort
{

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (0)
constexpr const char *INVALID_DEVICE_ID = "";

using core_op_priority_t = uint8_t;

constexpr const uint16_t SINGLE_CONTEXT_BATCH_SIZE = 1;


class ScheduledCoreOp
{
public:
    static Expected<std::shared_ptr<ScheduledCoreOp>> create(std::shared_ptr<CoreOp> added_core_op,
        StreamInfoVector &stream_infos);

    virtual ~ScheduledCoreOp()  = default;
    ScheduledCoreOp(const ScheduledCoreOp &other) = delete;
    ScheduledCoreOp &operator=(const ScheduledCoreOp &other) = delete;
    ScheduledCoreOp &operator=(ScheduledCoreOp &&other) = delete;
    ScheduledCoreOp(ScheduledCoreOp &&other) noexcept = delete;

    std::shared_ptr<CoreOp> get_core_op();
    const std::vector<stream_name_t> &get_outputs_names();
    const std::vector<stream_name_t> &get_inputs_names();

    uint32_t get_max_ongoing_frames_per_device() const;

    uint16_t get_min_input_buffers_count() const;
    uint16_t get_min_output_buffers_count() const;

    uint16_t get_max_batch_size() const;
    uint16_t get_burst_size() const;
    bool use_dynamic_batch_flow() const;

    device_id_t get_last_device();
    void set_last_device(const device_id_t &device_id);

    Expected<std::chrono::milliseconds> get_timeout(const stream_name_t &stream_name = "");
    hailo_status set_timeout(const std::chrono::milliseconds &timeout, const stream_name_t &stream_name = "");
    Expected<uint32_t> get_threshold(const stream_name_t &stream_name);
    hailo_status set_threshold(uint32_t threshold, const stream_name_t &stream_name = "");
    core_op_priority_t get_priority();
    void set_priority(core_op_priority_t priority);

    std::chrono::time_point<std::chrono::steady_clock> get_last_run_timestamp();
    void set_last_run_timestamp(const std::chrono::time_point<std::chrono::steady_clock> &timestamp);

    void mark_frame_sent();

    SchedulerCounter &pending_frames();
    uint32_t get_min_input_pending_frames() const;

    bool is_stream_enabled(const stream_name_t &stream_name) const;
    void enable_stream(const stream_name_t &stream_name);
    void disable_stream(const stream_name_t &stream_name);
    bool any_stream_disabled() const;
    bool all_stream_disabled() const;

    ScheduledCoreOp(std::shared_ptr<CoreOp> core_op, std::chrono::milliseconds timeout,
        uint16_t max_batch_size, bool use_dynamic_batch_flow, StreamInfoVector &stream_infos);

private:
    std::shared_ptr<CoreOp> m_core_op;
    std::chrono::time_point<std::chrono::steady_clock> m_last_run_time_stamp;
    std::chrono::milliseconds m_timeout;
    std::atomic_bool m_frame_was_sent;
    uint16_t m_max_batch_size;
    bool m_use_dynamic_batch_flow;

    // For each stream, amount of frames pending (for launch_transfer call)
    SchedulerCounter m_pending_frames;

    std::unordered_map<stream_name_t, std::atomic_uint32_t> m_min_threshold_per_stream;
    std::unordered_map<stream_name_t, std::atomic_bool> m_is_stream_enabled;

    core_op_priority_t m_priority;

    device_id_t m_last_device_id;

    std::vector<stream_name_t> m_inputs_names;
    std::vector<stream_name_t> m_outputs_names;
};


using ScheduledCoreOpPtr = std::shared_ptr<ScheduledCoreOp>;

} /* namespace hailort */

#endif /* _HAILO_SCHEDULED_CORE_OP_HPP_ */
