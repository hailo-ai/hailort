/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_scheduler.hpp
 * @brief Class declaration for NetworkGroupScheduler that schedules network groups to be active depending on the scheduling algorithm.
 **/

#ifndef _HAILO_SCHEDULED_NETWORK_GROUP_HPP_
#define _HAILO_SCHEDULED_NETWORK_GROUP_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/network_group.hpp"
#include "common/utils.hpp"
#include "common/filesystem.hpp"
#include "scheduler_mon.hpp"

#include <condition_variable>
#include <queue>


namespace hailort
{

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (0)

using stream_name_t = std::string;

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

class ScheduledNetworkGroup
{
public:
    static Expected<std::shared_ptr<ScheduledNetworkGroup>> create(std::shared_ptr<ConfiguredNetworkGroup> added_cng, StreamInfoVector &stream_infos);

    virtual ~ScheduledNetworkGroup()  = default;
    ScheduledNetworkGroup(const ScheduledNetworkGroup &other) = delete;
    ScheduledNetworkGroup &operator=(const ScheduledNetworkGroup &other) = delete;
    ScheduledNetworkGroup &operator=(ScheduledNetworkGroup &&other) = delete;
    ScheduledNetworkGroup(ScheduledNetworkGroup &&other) noexcept = delete;

    bool has_enough_space_in_read_buffers(uint32_t ongoing_frames);
    bool has_input_written_most_frames(const std::string &stream_name);
    std::unordered_map<stream_name_t, uint32_t> total_written_frames_count();
    bool has_pending_frames();
    bool can_stream_read(const std::string &stream_name);
    bool use_dynamic_batch_flow();
    bool has_ng_drained_everything(bool streaming_mode);
    void decrease_current_ng_counters();
    uint32_t get_pre_transfer_h2d_frames_count();

    std::string get_network_group_name();
    uint32_t get_h2d_transferred_frames_count();

    std::shared_ptr<ConfiguredNetworkGroup> get_network_group();

    void mark_frame_sent();

    std::chrono::time_point<std::chrono::steady_clock> get_last_run_timestamp();
    void set_last_run_timestamp(const std::chrono::time_point<std::chrono::steady_clock> &timestamp);

    Expected<std::chrono::milliseconds> get_timeout(const stream_name_t &stream_name = "");
    hailo_status set_timeout(const std::chrono::milliseconds &timeout, const stream_name_t &stream_name = "");
    Expected<uint32_t> get_threshold(const stream_name_t &stream_name);
    hailo_status set_threshold(uint32_t threshold, const stream_name_t &stream_name = "");

    uint16_t get_max_batch_size();

    Counter &requested_write_frames();
    std::atomic_uint32_t &requested_write_frames(const stream_name_t &stream_name);
    uint32_t requested_write_frames_max_value();
    Counter &finished_write_frames();
    std::atomic_uint32_t &finished_write_frames(const stream_name_t &stream_name);
    uint32_t finished_write_frames_min_value();

    Counter &h2d_requested_transferred_frames();
    std::atomic_uint32_t &h2d_requested_transferred_frames(const stream_name_t &stream_name);
    Counter &h2d_finished_transferred_frames();
    std::atomic_uint32_t &h2d_finished_transferred_frames(const stream_name_t &stream_name);

    Counter &requested_read_frames();
    std::atomic_uint32_t &requested_read_frames(const stream_name_t &stream_name);
    Counter &ongoing_read_frames();
    std::atomic_uint32_t &ongoing_read_frames(const stream_name_t &stream_name);

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

    ScheduledNetworkGroup(std::shared_ptr<ConfiguredNetworkGroup> cng, std::chrono::milliseconds timeout,
        uint16_t max_batch_size, StreamInfoVector &stream_infos, std::string network_group_name);

private:
    std::shared_ptr<ConfiguredNetworkGroup> m_cng;

    std::chrono::time_point<std::chrono::steady_clock> m_last_run_time_stamp;
    std::chrono::milliseconds m_timeout;

    std::atomic_bool m_frame_was_sent;
    uint16_t m_max_batch_size;

    Counter m_requested_write_frames; // 'wait_for_write()' has been called
    Counter m_finished_write_frames; // 'signal_finished_write()' has been called - frame is written in buffer (writes are a-sync)

    Counter m_h2d_requested_transferred_frames; // 'send_pending_buffer()' has been called
    Counter m_h2d_finished_transferred_frames; // Frame has been transferred to device (intrpt was raised)

    Counter m_requested_read_frames; // 'wait_for_read()' has been called
    Counter m_ongoing_read_frames; // 'wait_for_read()' has finished, the user is blocking on read (reads are sync)

    Counter m_d2h_finished_transferred_frames; // Frame has been transferred from device (intrpt was raised)
    Counter m_finished_read_frames; // 'signal_finish_read()' has been called - user finished getting the frame

    std::unordered_map<stream_name_t, std::atomic_uint32_t> m_min_threshold_per_stream;

    std::string m_network_group_name;

    std::vector<stream_name_t> m_inputs_names;
    std::vector<stream_name_t> m_outputs_names;

    std::unordered_map<stream_name_t, std::queue<uint32_t>> m_output_streams_read_orders;

    bool m_is_nms;
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULED_NETWORK_GROUP_HPP_ */
