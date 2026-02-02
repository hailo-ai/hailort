/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracker_core.hpp
 * @brief Core tracker logic
 **/

#ifndef _HAILOTRACKER_SRC_TRACKER_CORE_HPP_
#define _HAILOTRACKER_SRC_TRACKER_CORE_HPP_

#include "tracklet.hpp"
#include "association.hpp"
#include "kalman_filter.hpp"
#include "hailotracker.h"

#include <vector>
#include <mutex>

namespace hailotracker {

class Tracker {
public:
    Tracker(const hailo_tracker_config_t &config);
    ~Tracker() = default;
    
    void predict();
    std::vector<hailo_tracklet_t> &get_current_tracklets();
    void update(const hailo_detections_t *detections);

private:
    hailo_tracker_config_t m_config;
    uint32_t m_next_id;
    Association m_association;
    KalmanFilter m_kf;
    std::vector<Tracklet> m_tracklets;
    std::vector<hailo_tracklet_t> m_output_tracklets;
    mutable std::mutex m_mutex;
};

} // namespace hailotracker

#endif // _HAILOTRACKER_SRC_TRACKER_CORE_HPP_
