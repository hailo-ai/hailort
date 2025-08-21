/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file live_stats.hpp
 * @brief Live stats
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_LIVE_STATS_HPP_
#define _HAILO_HAILORTCLI_RUN2_LIVE_STATS_HPP_

#include "common/os_utils.hpp"
#include "hailo/event.hpp"
#include "hailo/expected.hpp"

#include <nlohmann/json.hpp>
#include <stdint.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>

class LiveStats final
{
public:
    class Track
    {
    public:
        Track() : m_started(false)
        {}

        hailo_status start();
        uint32_t push_text(std::stringstream &ss);
        void push_json(nlohmann::ordered_json &json);
        virtual hailort::Expected<double> get_last_measured_fps();

    protected:
        virtual hailo_status start_impl() = 0;
        virtual uint32_t push_text_impl(std::stringstream &ss) = 0;
        virtual void push_json_impl(nlohmann::ordered_json &json) = 0;

        bool m_started;
    };

    LiveStats(std::chrono::milliseconds interval, bool should_print);
    ~LiveStats();
    void add(std::shared_ptr<Track> track, uint8_t level); // prints tracks in consecutive order from low-to-high levels
    void measure_and_print();
    hailo_status dump_stats(const std::string &json_path, const std::string &inference_mode);
    hailo_status start();
    void stop();
    hailort::Expected<std::vector<double>> get_last_measured_fps_per_network_group();

private:
    bool m_running;
    std::chrono::milliseconds m_interval;
    bool m_should_print;
    hailort::EventPtr m_stop_event;
    std::map<uint8_t, std::vector<std::shared_ptr<Track>>> m_tracks;
    std::thread m_thread;
    std::mutex m_mutex;
    uint32_t m_prev_count;
    hailort::CursorAdjustment m_enable_ansi_escape_sequences;
};

#endif /* _HAILO_HAILORTCLI_RUN2_LIVE_STATS_HPP_ */
