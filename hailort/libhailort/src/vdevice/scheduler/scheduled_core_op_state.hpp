/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_scheduler.hpp
 * @brief Class declaration for CoreOpsScheduler that schedules core-ops to be active depending on the scheduling algorithm.
 **/

#ifndef _HAILO_SCHEDULED_CORE_OP_HPP_
#define _HAILO_SCHEDULED_CORE_OP_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/network_rate_calculator.hpp"

#include "common/utils.hpp"

#include "core_op/core_op.hpp"

#include <condition_variable>
#include <queue>


namespace hailort
{

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (0)
#define INVALID_DEVICE_ID (UINT32_MAX)

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

    bool has_enough_space_in_read_buffers(uint32_t ongoing_frames);
    uint16_t get_min_input_buffers_count(uint32_t device_count);
    bool has_input_written_most_frames(const std::string &stream_name);
    std::unordered_map<stream_name_t, uint32_t> total_written_frames_count();
    bool has_pending_frames();
    bool can_stream_read(const std::string &stream_name);
    bool can_stream_write(const std::string &stream_name);
    bool use_dynamic_batch_flow();
    bool has_core_op_drained_everything();
    void decrease_current_core_op_counters();
    uint32_t get_pre_transfer_h2d_frames_count();

    bool is_ready_to_switch();
    void mark_ready_to_switch();
    void mark_unready_to_switch();

    std::string get_core_op_name();

    std::shared_ptr<CoreOp> get_core_op();

    void mark_frame_sent();

    std::chrono::time_point<std::chrono::steady_clock> get_last_run_timestamp();
    void set_last_run_timestamp(const std::chrono::time_point<std::chrono::steady_clock> &timestamp);

    Expected<std::chrono::milliseconds> get_timeout(const stream_name_t &stream_name = "");
    hailo_status set_timeout(const std::chrono::milliseconds &timeout, const stream_name_t &stream_name = "");
    Expected<uint32_t> get_threshold(const stream_name_t &stream_name);
    hailo_status set_threshold(uint32_t threshold, const stream_name_t &stream_name = "");

    core_op_priority_t get_priority();
    void set_priority(core_op_priority_t priority);

    uint32_t get_last_device_index();
    void set_last_device_index(uint32_t device_index);

    uint16_t get_max_batch_size();

    Counter &requested_write_frames();
    std::atomic_uint32_t &requested_write_frames(const stream_name_t &stream_name);
    Counter &finished_write_frames();
    std::atomic_uint32_t &finished_write_frames(const stream_name_t &stream_name);
    uint32_t finished_write_frames_min_value();

    Counter &h2d_finished_transferred_frames();
    std::atomic_uint32_t &h2d_finished_transferred_frames(const stream_name_t &stream_name);

    Counter &requested_read_frames();
    std::atomic_uint32_t &requested_read_frames(const stream_name_t &stream_name);

    Counter &d2h_finished_transferred_frames();
    std::atomic_uint32_t &d2h_finished_transferred_frames(const stream_name_t &stream_name);
    Counter &finished_read_frames();
    std::atomic_uint32_t &finished_read_frames(const stream_name_t &stream_name);
    uint32_t finished_read_frames_min_value();

    const std::vector<stream_name_t> &get_outputs_names();
    const std::vector<stream_name_t> &get_inputs_names();

    bool is_nms()
    {
        return m_is_nms;
    }

    void push_device_index(uint32_t device_index);
    uint32_t pop_device_index(const stream_name_t &stream_name);

    ScheduledCoreOp(std::shared_ptr<CoreOp> core_op, std::chrono::milliseconds timeout,
        uint16_t max_batch_size, StreamInfoVector &stream_infos, std::string core_op_name);

private:
    std::shared_ptr<CoreOp> m_core_op;

    std::chrono::time_point<std::chrono::steady_clock> m_last_run_time_stamp;
    std::chrono::milliseconds m_timeout;

    std::atomic_bool m_frame_was_sent;
    uint16_t m_max_batch_size;

    Counter m_requested_write_frames; // 'wait_for_write()' has been called
    Counter m_finished_write_frames; // 'signal_finished_write()' has been called - frame is written in buffer (writes are a-sync)

    Counter m_h2d_finished_transferred_frames; // Frame has been transferred to device (intrpt was raised)

    Counter m_requested_read_frames; // 'wait_for_read()' has been called

    Counter m_d2h_finished_transferred_frames; // Frame has been transferred from device (intrpt was raised)
    Counter m_finished_read_frames; // 'signal_finish_read()' has been called - user finished getting the frame

    std::unordered_map<stream_name_t, std::atomic_uint32_t> m_min_threshold_per_stream;

    core_op_priority_t m_priority;

    std::atomic_uint32_t m_last_device_index;

    std::string m_core_op_name;

    std::vector<stream_name_t> m_inputs_names;
    std::vector<stream_name_t> m_outputs_names;

    std::unordered_map<stream_name_t, std::queue<uint32_t>> m_output_streams_read_orders;

    bool m_is_nms;

    // TODO: Remove this flag when the old scheduling mode will be deprecated
    std::atomic_bool m_ready_to_switch;
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULED_CORE_OP_HPP_ */
