/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file live_printer.hpp
 * @brief Live printer
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_LIVE_PRINTER_HPP_
#define _HAILO_HAILORTCLI_RUN2_LIVE_PRINTER_HPP_

#include "common/os_utils.hpp"
#include "hailo/event.hpp"
#include <stdint.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>

class LivePrinter final
{
public:
    class Track
    {
    public:
        Track() : m_started(false)
        {}

        virtual hailo_status start() = 0;
        virtual uint32_t get_text(std::stringstream &ss) = 0;

    protected:
        bool m_started;
    };

    LivePrinter(std::chrono::milliseconds interval);
    ~LivePrinter();
    void add(std::shared_ptr<Track> track, uint8_t level); // prints tracks in consecutive order from low-to-high levels
    void print();
    hailo_status start();

private:
    std::chrono::milliseconds m_interval;
    hailort::EventPtr m_stop_event;
    std::map<uint8_t, std::vector<std::shared_ptr<Track>>> m_tracks;
    std::thread m_thread;
    std::mutex m_mutex;
    uint32_t m_prev_count;
    hailort::CursorAdjustment m_enable_ansi_escape_sequences;
};

#endif /* _HAILO_HAILORTCLI_RUN2_LIVE_PRINTER_HPP_ */