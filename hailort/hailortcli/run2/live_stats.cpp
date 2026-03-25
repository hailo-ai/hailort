/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file live_stats.cpp
 * @brief Live stats
 **/

#include "live_stats.hpp"
#include "../common.hpp"
#include "common/os_utils.hpp"
#include "common/utils.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

using namespace hailort;

const uint8_t NETWORK_STATS_LEVEL = 1;

hailo_status LiveStats::Track::start()
{
    CHECK_SUCCESS(start_impl());
    m_started = true;
    return HAILO_SUCCESS;
}

std::string LiveStats::Track::get_text() const
{
    return m_started ? get_text_impl() : "";
}

void LiveStats::Track::push_json(nlohmann::ordered_json &json)
{
    if (!m_started) {
        return;
    }
    push_json_impl(json);
}

Expected<double> LiveStats::Track::get_last_measured_fps()
{
    // This virtual getter is supported only for the derived class NetworkLiveTrack
    return make_unexpected(HAILO_NOT_AVAILABLE);
}


LiveStats::LiveStats(std::chrono::milliseconds interval, bool should_print) :
    m_running(false),
    m_interval(interval),
    m_should_print(should_print),
    m_stop_event(),
    m_tracks(),
    m_mutex(),
    m_prev_line_count(0),
    m_enable_ansi_escape_sequences(CursorAdjustment())
{
    auto event_exp = Event::create_shared(Event::State::not_signalled);
    assert(event_exp);
    m_stop_event = event_exp.release();
}

LiveStats::~LiveStats()
{
    stop();
    measure_and_print();
}

void LiveStats::add(std::shared_ptr<Track> track, uint8_t level)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_tracks[level].emplace_back(track);
}

void LiveStats::measure_and_print()
{
    measure();
    if (m_should_print) {
        print_measurements();
    }
}

void LiveStats::measure()
{
    for (auto &level_pair : m_tracks) {
        for (auto &track : level_pair.second) {
            track->measure();
        }
    }
}

void LiveStats::print_measurements()
{
    std::string s;
    for (const auto &[key, level_tracks] : m_tracks) {
        for (const auto &track : level_tracks) {
            s += track->get_text();
        }
    }
    // On the first print m_prev_count = 0, so no lines will be deleted
    CliCommon::reset_cursor(m_prev_line_count);
    std::cout << s << std::flush;
    m_prev_line_count = std::count(s.begin(), s.end(), '\n');
}

hailo_status LiveStats::dump_stats(const std::string &json_path, const std::string &inference_mode)
{
    stop(); // stop measuring before creating json because we want the json to hold the last measurements
    nlohmann::ordered_json json;

    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto str_time = std::string(std::ctime(&time));
    if (str_time.length()){
        str_time.pop_back();
    }

    json["time"] = str_time;
    json["inference_mode"] = inference_mode;
    json["network_groups"] = nlohmann::ordered_json::array();

    std::unique_lock<std::mutex> lock(m_mutex);
    for (auto &level_pair : m_tracks) {
            for (auto &track : level_pair.second) {
                track->push_json(json);
            }
    }

    std::ofstream output_json(json_path);
    CHECK(output_json, HAILO_FILE_OPERATION_FAILURE, "Failed opening file '{}'", json_path);

    output_json << std::setw(4) << json << std::endl; // 4: amount of spaces to indent (for pretty printing)
    CHECK(!output_json.bad() && !output_json.fail(), HAILO_FILE_OPERATION_FAILURE,
        "Failed writing to file '{}'", json_path);

    return HAILO_SUCCESS;
}

Expected<std::vector<double>> LiveStats::get_last_measured_fps_per_network_group()
{
    std::vector<double> last_measured_fpss;
    CHECK_AS_EXPECTED(contains(m_tracks, NETWORK_STATS_LEVEL), HAILO_NOT_AVAILABLE);

    for (size_t network_stats_track_index = 0; network_stats_track_index < m_tracks[NETWORK_STATS_LEVEL].size(); network_stats_track_index++) {
        TRY(auto fps,
            m_tracks[NETWORK_STATS_LEVEL][network_stats_track_index]->get_last_measured_fps());
        last_measured_fpss.emplace_back(fps);
    }

    return last_measured_fpss;
}

hailo_status LiveStats::start()
{
    // In order to re-start LiveStats, we should add m_stop_event->reset() here
    m_running = true;
    for (auto &level_pair : m_tracks) {
        for (auto &track : level_pair.second) {
            CHECK_SUCCESS(track->start());
        }
    }

    m_thread = std::thread([this] () {
        OsUtils::set_current_thread_name("LIVE_PRINTER");
        while (true) {
            measure_and_print();
            auto status = m_stop_event->wait(m_interval);
            if (HAILO_TIMEOUT != status) {
                break;
            }
        }
    });
    return HAILO_SUCCESS;
}

void LiveStats::stop()
{
    if (m_running){
        (void)m_stop_event->signal();
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_running = false;
    }
}
