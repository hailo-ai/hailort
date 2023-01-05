/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_live_track.cpp
 * @brief Network live track
 **/

#include "network_live_track.hpp"
#include <spdlog/fmt/fmt.h>
#include <sstream>

NetworkLiveTrack::NetworkLiveTrack(const std::string &name) :
    m_name(name), m_count(0), m_last_get_time(std::chrono::steady_clock::now())
{
}

uint32_t NetworkLiveTrack::get_text(std::stringstream &ss)
{
    auto elapsed_time = std::chrono::steady_clock::now() - m_last_get_time;
    auto count = m_count.load();

    auto fps = count / std::chrono::duration<double>(elapsed_time).count();
    ss << fmt::format("{} - fps: {:.2f}\n", m_name, fps);

    return 1;
}

void NetworkLiveTrack::progress()
{
    m_count++;
}