/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracklet.hpp
 * @brief Tracklet class for object tracking
 **/

#ifndef _HAILOTRACKER_SRC_TRACKLET_HPP_
#define _HAILOTRACKER_SRC_TRACKLET_HPP_

#include "hailotracker.h"
#include "kalman_filter.hpp"
#include "eigen_wrapper.hpp"
#include "expected.hpp"

namespace hailotracker {

class Tracklet {
public:
    Tracklet(const hailo_detection_t &det);
    ~Tracklet() = default;

    // Basic accessors
    uint32_t id() const { return m_id; }
    hailo_tracklet_state_t state() const { return m_state; }
    uint32_t hits() const { return m_hits; }
    uint32_t frames_since_update() const { return m_frames_since_update; }
    uint32_t total_frames() const { return m_total_frames; }
    
    // Detection accessors
    const hailo_detection_t &smoothed_detection() const { return m_smoothed_detection; }
    const hailo_detection_t &last_detection() const { return m_last_detection; }
    
    // Kalman state accessors
    KalmanState &kf_state() { return m_kf_state; }
    float32_t position_x() const { return m_kf_state.x(0); }
    float32_t position_y() const { return m_kf_state.x(1); }
    float32_t velocity_x() const { return m_kf_state.x(2); }
    float32_t velocity_y() const { return m_kf_state.x(3); }
    
    // Update methods
    void update_detection(const hailo_detection_t &det);
    hailopp::Expected<hailopp_status> update_smoothed_from_kalman(float32_t smoothing_alpha);
    void increment_age();
    
    // State transition methods
    void confirm_track(uint32_t id);
    void mark_lost();
    void reactivate();
    
    // State query methods
    bool should_confirm(uint8_t min_confirmed_frames) const;
    bool should_mark_lost(uint8_t max_missed_frames) const;
    bool should_delete(uint8_t aging_threshold) const;

private:
    uint32_t m_id;
    uint32_t m_hits;
    uint32_t m_frames_since_update;
    uint32_t m_total_frames;
    hailo_tracklet_state_t m_state;
    hailo_detection_t m_smoothed_detection;
    hailo_detection_t m_last_detection;
    KalmanState m_kf_state;
};

} // namespace hailotracker

#endif // _HAILOTRACKER_SRC_TRACKLET_HPP_
