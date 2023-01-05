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
#include <sstream>
#include <iostream>
using namespace hailort;

LivePrinter::LivePrinter(std::chrono::milliseconds interval) :
    m_interval(interval),
    m_stop_event(Event::create_shared(Event::State::not_signalled)),
    m_tracks(),
    m_mutex()
{
}

LivePrinter::~LivePrinter()
{
    (void)m_stop_event->signal();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    print(false);
}

void LivePrinter::add(std::shared_ptr<Track> track)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_tracks.emplace_back(track);
}

void LivePrinter::print(bool reset)
{
    std::stringstream ss;
    uint32_t count = 0;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto &track : m_tracks) {
            count += track->get_text(ss);
        }
    }

    std::cout << ss.str() << std::flush;
    if (reset) {
        CliCommon::reset_cursor(count);
        //TODO: what aout leftovers from prev line?
    }
}

void LivePrinter::start()
{
    m_thread = std::thread([this] () {
        while (true) {
            print(true);
            auto status = m_stop_event->wait(m_interval);
            if (HAILO_TIMEOUT != status) {
                break;
            }
        }
    });
}