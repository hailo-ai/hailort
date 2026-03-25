/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
#include "expected.hpp"

#include <vector>
#include <memory>
#include <mutex>

namespace hailotracker {

class Tracker {
public:
    static hailopp::Expected<std::unique_ptr<Tracker>> create(const hailo_tracker_config_t &config);
    ~Tracker() = default;

    hailopp::Expected<hailopp_status> predict();
    std::vector<hailo_tracklet_t> &get_current_tracklets();
    hailopp::Expected<hailopp_status> update(const hailo_detections_t *detections);

private:
    explicit Tracker(const hailo_tracker_config_t &config);

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
