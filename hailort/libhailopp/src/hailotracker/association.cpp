/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file association.cpp
 * @brief Data association algorithms implementation
 **/

#include "association.hpp"

#include <algorithm>
#include <cmath>

using namespace hailopp;

namespace hailotracker {

constexpr int NO_MATCH = -1;

float32_t compute_iou(const hailo_detection_t &det1, const hailo_detection_t &det2)
{
    const float32_t inter_w = std::min(det1.x_max, det2.x_max) - std::max(det1.x_min, det2.x_min);
    const float32_t inter_h = std::min(det1.y_max, det2.y_max) - std::max(det1.y_min, det2.y_min);
    
    if ((0.0f >= inter_w) || (0.0f >= inter_h)) {
        return 0.0f;
    }
    
    const float32_t inter_area = inter_w * inter_h;
    const float32_t area1 = (det1.x_max - det1.x_min) * (det1.y_max - det1.y_min);
    const float32_t area2 = (det2.x_max - det2.x_min) * (det2.y_max - det2.y_min);
    const float32_t union_area = area1 + area2 - inter_area;
    
    return (inter_area / union_area);
}

AssociationConfig::AssociationConfig(const hailo_tracker_config_t &cfg)
    : iou_threshold(cfg.association_threshold)
    , iou_weight(cfg.iou_weight)
    , is_class_aware(cfg.class_aware_tracking)
    , max_tracklets(cfg.max_tracklets)
{
}

Association::Association(const AssociationConfig &config)
    : m_config(config)
{
    m_result.matches.reserve(m_config.max_tracklets);
    m_result.unmatched_detections.reserve(m_config.max_tracklets);
    m_result.unmatched_tracklets.reserve(m_config.max_tracklets);
    m_matched_trks.reserve(m_config.max_tracklets);
}

Expected<hailopp_status> Association::run(const hailo_detections_t *detections, std::vector<Tracklet> &tracklets)
{
    if (!detections) {
        return make_unexpected(HAILOPP_INVALID_ARGUMENT);
    }

    m_result.matches.clear();
    m_result.unmatched_detections.clear();
    m_result.unmatched_tracklets.clear();

    if ((0 == detections->count) || (tracklets.empty())) {
        // No matching possible - mark all as unmatched
        for (size_t i = 0; i < detections->count; ++i) {
            m_result.unmatched_detections.emplace_back(&detections->detections[i]);
        }
        for (auto &tracklet : tracklets) {
            m_result.unmatched_tracklets.emplace_back(&tracklet);
        }
        return HAILOPP_SUCCESS;
    }
    
    // Greedy matching: for each detection, find best tracklet
    m_matched_trks.assign(tracklets.size(), false);
    
    for (size_t i = 0; i < detections->count; ++i) {
        float32_t best_iou = m_config.iou_threshold;
        int best_trk = NO_MATCH;
        
        // Find best tracklet for this detection
        for (size_t j = 0; j < tracklets.size(); ++j) {
            if (m_matched_trks[j]) continue;  // Skip already matched tracklets
            
            // Class-aware filtering
            if (m_config.is_class_aware && 
                (tracklets[j].smoothed_detection().class_id != detections->detections[i].class_id)) {
                continue;
            }
            
            float32_t iou = compute_iou(detections->detections[i], tracklets[j].smoothed_detection());
            if (best_iou < iou) {
                best_iou = iou;
                best_trk = static_cast<int>(j);
            }
        }
        
        if (NO_MATCH != best_trk) {
            // Found a match
            m_result.matches.emplace_back(&detections->detections[i], &tracklets[best_trk]);
            m_matched_trks[best_trk] = true;
        } else {
            // No match for this detection
            m_result.unmatched_detections.emplace_back(&detections->detections[i]);
        }
    }
    
    // Collect unmatched tracklets
    for (size_t j = 0; j < tracklets.size(); ++j) {
        if (!m_matched_trks[j]) {
            m_result.unmatched_tracklets.emplace_back(&tracklets[j]);
        }
    }

    return HAILOPP_SUCCESS;
}

} // namespace hailotracker

