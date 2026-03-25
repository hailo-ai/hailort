/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file tracker_core.cpp
 * @brief Core tracker logic implementation
 **/

#include "tracker_core.hpp"

#include <algorithm>

using namespace hailopp;

namespace hailotracker {

static Expected<hailopp_status> validate_config(const hailo_tracker_config_t &config)
{
    if (0 == config.max_tracklets) {
        return make_unexpected(HAILOPP_INVALID_ARGUMENT);
    }
    if ((0.0f > config.smoothing_alpha) || (1.0f < config.smoothing_alpha)) {
        return make_unexpected(HAILOPP_INVALID_ARGUMENT);
    }
    return HAILOPP_SUCCESS;
}

Expected<std::unique_ptr<Tracker>> Tracker::create(const hailo_tracker_config_t &config)
{
    auto status = validate_config(config);
    if (!status.has_value()) {
        return make_unexpected(status.error());
    }

    auto tracker = std::unique_ptr<Tracker>(new (std::nothrow) Tracker(config));
    if (!tracker) {
        return make_unexpected(HAILOPP_OUT_OF_HOST_MEMORY);
    }
    return tracker;
}

Tracker::Tracker(const hailo_tracker_config_t &config)
    : m_config(config), m_next_id(1),
      m_association(AssociationConfig(config)),
      m_kf(config.position_std_weight, config.velocity_std_weight)
{
    m_tracklets.reserve(m_config.max_tracklets);
    m_output_tracklets.reserve(m_config.max_tracklets);
}


Expected<hailopp_status> Tracker::predict()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto &tracklet : m_tracklets) {
        // Only predict for TRACKED and LOST tracklets
        if ((HAILO_TRACKLET_STATE_TRACKED == tracklet.state()) ||
            (HAILO_TRACKLET_STATE_LOST == tracklet.state())) {

            if (m_config.enable_kalman_filter) {
                m_kf.predict(tracklet.kf_state());
                // Update smoothed detection with Kalman predicted position (EMA)
                auto status = tracklet.update_smoothed_from_kalman(m_config.smoothing_alpha);
                if (!status.has_value()) {
                    return make_unexpected(status.error());
                }
            }
        }
    }

    return HAILOPP_SUCCESS;
}

std::vector<hailo_tracklet_t> &Tracker::get_current_tracklets()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_output_tracklets.clear();

    for (auto &tracklet : m_tracklets) {
        hailo_tracklet_t output_tracklet;
        output_tracklet.id = tracklet.id();
        output_tracklet.detection = tracklet.smoothed_detection();
        output_tracklet.state = tracklet.state();
        output_tracklet.frames_since_update = tracklet.frames_since_update();
        output_tracklet.total_frames_tracked = tracklet.total_frames();
        output_tracklet.velocity_x = tracklet.velocity_x();
        output_tracklet.velocity_y = tracklet.velocity_y();

        m_output_tracklets.emplace_back(output_tracklet);
    }

    return m_output_tracklets;
}

Expected<hailopp_status> Tracker::update(const hailo_detections_t *detections)
{
    if (!detections) {
        return make_unexpected(HAILOPP_INVALID_ARGUMENT);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Perform association
    auto assoc_status = m_association.run(detections, m_tracklets);
    if (!assoc_status.has_value()) {
        return make_unexpected(assoc_status.error());
    }
    const auto &assoc_result = m_association.result();
    
    // Update matched tracklets
    for (const auto &[det_ptr, tracklet_ptr] : assoc_result.matches) {
        tracklet_ptr->update_detection(*det_ptr);
        
        // Check state transitions
        if (tracklet_ptr->should_confirm(m_config.min_confirmed_frames)) {
            tracklet_ptr->confirm_track(m_next_id++);
            
            // Initialize Kalman state for newly confirmed tracklet
            if (m_config.enable_kalman_filter) {
                m_kf.init_state(tracklet_ptr->kf_state(), *det_ptr);
            }
        }
        
        if (HAILO_TRACKLET_STATE_LOST == tracklet_ptr->state()) {
            tracklet_ptr->reactivate();
        }
        
        // Update Kalman state with measurement
        if (m_config.enable_kalman_filter && (HAILO_TRACKLET_STATE_TRACKED == tracklet_ptr->state())) {
            m_kf.update(tracklet_ptr->kf_state(), *det_ptr);
        }
    }
    
    // Age unmatched tracklets
    for (auto *tracklet_ptr : assoc_result.unmatched_tracklets) {
        tracklet_ptr->increment_age();
        
        if (tracklet_ptr->should_mark_lost(m_config.max_missed_frames)) {
            tracklet_ptr->mark_lost();
        }
    }
    
    // Create new tracklets for unmatched detections
    for (const auto *det_ptr : assoc_result.unmatched_detections) {
        if (m_config.add_threshold <= det_ptr->score) {
            if (m_config.max_tracklets > m_tracklets.size()) {
                m_tracklets.emplace_back(*det_ptr);
            }
        }
    }
    
    // Remove old tracklets
    m_tracklets.erase(
        std::remove_if(m_tracklets.begin(), m_tracklets.end(),
            [this](const Tracklet &tracklet) {
                return tracklet.should_delete(m_config.aging_threshold);
            }),
        m_tracklets.end());

    return HAILOPP_SUCCESS;
}

} // namespace hailotracker

