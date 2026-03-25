/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracklet.cpp
 * @brief Tracklet class implementation
 **/

#include "tracklet.hpp"

using namespace hailopp;

namespace hailotracker {

Tracklet::Tracklet(const hailo_detection_t &det)
    : m_id(0), m_hits(1), m_frames_since_update(0), m_total_frames(1),
      m_state(HAILO_TRACKLET_STATE_NEW), m_smoothed_detection(det), m_last_detection(det)
{
    // Initialize Kalman state with detection center
    float32_t cx = (det.x_min + det.x_max) * 0.5f;
    float32_t cy = (det.y_min + det.y_max) * 0.5f;
    m_kf_state.x(0) = cx;
    m_kf_state.x(1) = cy;
}

void Tracklet::update_detection(const hailo_detection_t &det)
{
    m_hits++;
    m_frames_since_update = 0;
    m_total_frames++;
    
    // Only update last detection
    m_last_detection = det;
}

Expected<hailopp_status> Tracklet::update_smoothed_from_kalman(float32_t smoothing_alpha)
{
    if ((0.0f > smoothing_alpha) || (1.0f < smoothing_alpha)) {
        return make_unexpected(HAILOPP_INVALID_ARGUMENT);
    }

    // Update smoothed_detection bbox using Kalman predicted position with EMA
    float32_t pred_x = m_kf_state.x(0);
    float32_t pred_y = m_kf_state.x(1);
    
    // Keep current bbox size from last detection
    float32_t width = m_last_detection.x_max - m_last_detection.x_min;
    float32_t height = m_last_detection.y_max - m_last_detection.y_min;
    float32_t half_width = width * 0.5f;
    float32_t half_height = height * 0.5f;
    
    // Calculate new bbox from Kalman predicted center
    float32_t new_x_min = pred_x - half_width;
    float32_t new_y_min = pred_y - half_height;
    float32_t new_x_max = pred_x + half_width;
    float32_t new_y_max = pred_y + half_height;
    
    // Apply EMA smoothing
    const float32_t alpha = smoothing_alpha;
    const float32_t one_minus_alpha = 1.0f - alpha;
    m_smoothed_detection.x_min = alpha * new_x_min + one_minus_alpha * m_smoothed_detection.x_min;
    m_smoothed_detection.y_min = alpha * new_y_min + one_minus_alpha * m_smoothed_detection.y_min;
    m_smoothed_detection.x_max = alpha * new_x_max + one_minus_alpha * m_smoothed_detection.x_max;
    m_smoothed_detection.y_max = alpha * new_y_max + one_minus_alpha * m_smoothed_detection.y_max;
    
    // Copy class_id and score from last detection
    m_smoothed_detection.class_id = m_last_detection.class_id;
    m_smoothed_detection.score = m_last_detection.score;

    return HAILOPP_SUCCESS;
}

void Tracklet::increment_age()
{
    m_frames_since_update++;
    m_total_frames++;
}

void Tracklet::confirm_track(uint32_t id)
{
    if (HAILO_TRACKLET_STATE_NEW == m_state) {
        m_state = HAILO_TRACKLET_STATE_TRACKED;
        m_id = id;
    }
}

void Tracklet::mark_lost()
{
    if (HAILO_TRACKLET_STATE_TRACKED == m_state) {
        m_state = HAILO_TRACKLET_STATE_LOST;
    }
}

void Tracklet::reactivate()
{
    if (HAILO_TRACKLET_STATE_LOST == m_state) {
        m_state = HAILO_TRACKLET_STATE_TRACKED;
    }
}

bool Tracklet::should_confirm(uint8_t min_confirmed_frames) const
{
    return ((HAILO_TRACKLET_STATE_NEW == m_state) && (min_confirmed_frames <= m_hits));
}

bool Tracklet::should_mark_lost(uint8_t max_missed_frames) const
{
    return ((max_missed_frames < m_frames_since_update) && 
           (HAILO_TRACKLET_STATE_TRACKED == m_state));
}

bool Tracklet::should_delete(uint8_t aging_threshold) const
{
    return (aging_threshold < m_frames_since_update);
}

} // namespace hailotracker

