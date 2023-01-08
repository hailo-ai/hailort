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

#include "hailo/event.hpp"
#include <stdint.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

class LivePrinter final
{
public:
    class Track
    {
    public:
        virtual uint32_t get_text(std::stringstream &ss) = 0;
    };

    LivePrinter(std::chrono::milliseconds interval);
    ~LivePrinter();
    void add(std::shared_ptr<Track> track);
    void print(bool reset);
    void start();

private:
    std::chrono::milliseconds m_interval;
    hailort::EventPtr m_stop_event;
    std::vector<std::shared_ptr<Track>> m_tracks;
    std::thread m_thread;
    std::mutex m_mutex;
};

#endif /* _HAILO_HAILORTCLI_RUN2_LIVE_PRINTER_HPP_ */