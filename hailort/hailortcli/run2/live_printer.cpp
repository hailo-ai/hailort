/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file live_printer.cpp
 * @brief Live printer
 **/

#include "live_printer.hpp"
#include "../common.hpp"
#include "common/os_utils.hpp"
#include "common/utils.hpp"
#include <sstream>
#include <iostream>

using namespace hailort;

LivePrinter::LivePrinter(std::chrono::milliseconds interval) :
    m_interval(interval),
    m_stop_event(Event::create_shared(Event::State::not_signalled)),
    m_tracks(),
    m_mutex(),
    m_prev_count(0),
    m_enable_ansi_escape_sequences(CursorAdjustment())
{
}

LivePrinter::~LivePrinter()
{
    (void)m_stop_event->signal();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    print();
}

void LivePrinter::add(std::shared_ptr<Track> track, uint8_t level)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!contains(m_tracks, level)) {
        m_tracks[level] = {};
    }
    m_tracks[level].emplace_back(track);
}

void LivePrinter::print()
{
    std::stringstream ss;
    uint32_t count = 0;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto &level_pair : m_tracks) {
            for (auto &track : level_pair.second) {
                count += track->get_text(ss);
            }
        }
    }
    CliCommon::reset_cursor(m_prev_count); 
    // On the first print m_prev_count = 0, so no lines will be deleted
    std::cout << ss.str() << std::flush;
    m_prev_count = count;
}

hailo_status LivePrinter::start()
{
    for (auto &level_pair : m_tracks) {
        for (auto &track : level_pair.second) {
            CHECK_SUCCESS(track->start());
        }
    }
    
    m_thread = std::thread([this] () {
        OsUtils::set_current_thread_name("LIVE_PRINTER");
        while (true) {
            print();
            auto status = m_stop_event->wait(m_interval);
            if (HAILO_TIMEOUT != status) {
                break;
            }
        }
    });

    return HAILO_SUCCESS;
}
