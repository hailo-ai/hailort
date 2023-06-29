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

#include "scheduler_base.hpp"

#include <condition_variable>
#include <queue>


namespace hailort
{

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (0)
#define INVALID_DEVICE_ID (std::to_string(UINT32_MAX))

using stream_name_t = std::string;
using core_op_priority_t = uint8_t;

#define SINGLE_CONTEXT_BATCH_SIZE (1)

class Counter
{
public:
    Counter() : m_map()
        {};

    void insert(const stream_name_t &name)
    {
        assert(!contains(m_map, name));
        m_map[name] = 0;
    }

    std::atomic_uint32_t &operator [](const stream_name_t &name)
    {
        assert(contains(m_map, name));
        return m_map[name];
    }

    void increase(const stream_name_t &name)
    {
        assert(contains(m_map, name));
        m_map[name]++;
    }

    void decrease(const stream_name_t &name)
    {
        assert(contains(m_map, name));
        if (0 != m_map[name]) {
            m_map[name]--;
        }
    }

    uint32_t get_min_value()
    {
        return get_min_value_of_unordered_map(m_map);
    }

    uint32_t get_max_value()
    {
        return get_max_value_of_unordered_map(m_map);
    }

    bool all_values_bigger_or_equal(uint32_t value)
    {
        for (const auto &pair : m_map) {
            if (value > pair.second) {
                return false;
            }
        }
        return true;
    }

    bool empty()
    {
        for (const auto &pair : m_map) {
            if (0 != pair.second) {
                return false;
            }
        }
        return true;
    }

private:
    std::unordered_map<stream_name_t, std::atomic_uint32_t> m_map;
};

class ScheduledCoreOp
{
public:
    static Expected<std::shared_ptr<ScheduledCoreOp>> create(std::shared_ptr<CoreOp> added_core_op, StreamInfoVector &stream_infos);

    virtual ~ScheduledCoreOp()  = default;
    ScheduledCoreOp(const ScheduledCoreOp &other) = delete;
    ScheduledCoreOp &operator=(const ScheduledCoreOp &other) = delete;
    ScheduledCoreOp &operator=(ScheduledCoreOp &&other) = delete;
    ScheduledCoreOp(ScheduledCoreOp &&other) noexcept = delete;

    std::string get_core_op_name();
    std::shared_ptr<CoreOp> get_core_op();
    const std::vector<stream_name_t> &get_outputs_names();
    const std::vector<stream_name_t> &get_inputs_names();

    uint16_t get_min_input_buffers_count();
    uint16_t get_min_output_buffers_count();

    uint16_t get_max_batch_size();
    bool use_dynamic_batch_flow();
    bool has_core_op_drained_everything();

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
    void decrease_current_core_op_counters();

    Counter &pending_to_send_frames();
    std::atomic_uint32_t &pending_to_send_frames(const stream_name_t &stream_name);
    uint32_t pending_to_send_frames_min_value();

    Counter &h2d_finished_transferred_frames();
    std::atomic_uint32_t &h2d_finished_transferred_frames(const stream_name_t &stream_name);
    uint32_t h2d_finished_transferred_frames_max_value();

    Counter &requested_read_frames();
    std::atomic_uint32_t &requested_read_frames(const stream_name_t &stream_name);

    Counter &d2h_finished_transferred_frames();
    std::atomic_uint32_t &d2h_finished_transferred_frames(const stream_name_t &stream_name);

    Counter &finished_read_frames();
    std::atomic_uint32_t &finished_read_frames(const stream_name_t &stream_name);
    uint32_t finished_read_frames_min_value();


    bool is_nms()
    {
        return m_is_nms;
    }

    ScheduledCoreOp(std::shared_ptr<CoreOp> core_op, std::chrono::milliseconds timeout,
        uint16_t max_batch_size, bool use_dynamic_batch_flow, StreamInfoVector &stream_infos, std::string core_op_name);

private:
    std::shared_ptr<CoreOp> m_core_op;
    std::chrono::time_point<std::chrono::steady_clock> m_last_run_time_stamp;
    std::chrono::milliseconds m_timeout;
    std::atomic_bool m_frame_was_sent;
    uint16_t m_max_batch_size;
    bool m_use_dynamic_batch_flow;

    Counter m_pending_to_send_frames; // 'signal_frame_pending_to_send()' has been called - frame is written in buffer (writes are a-sync)

    Counter m_h2d_finished_transferred_frames; // Frame has been transferred to device (intrpt was raised)

    Counter m_requested_read_frames; // 'wait_for_read()' has been called

    Counter m_d2h_finished_transferred_frames; // Frame has been transferred from device (intrpt was raised)
    Counter m_finished_read_frames; // 'signal_finish_read()' has been called - user finished getting the frame

    std::unordered_map<stream_name_t, std::atomic_uint32_t> m_min_threshold_per_stream;

    core_op_priority_t m_priority;

    device_id_t m_last_device_id;

    std::string m_core_op_name;

    std::vector<stream_name_t> m_inputs_names;
    std::vector<stream_name_t> m_outputs_names;

    bool m_is_nms;
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULED_CORE_OP_HPP_ */
