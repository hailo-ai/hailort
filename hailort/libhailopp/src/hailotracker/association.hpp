/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file association.hpp
 * @brief Data association algorithms
 **/

#ifndef _HAILOTRACKER_SRC_ASSOCIATION_HPP_
#define _HAILOTRACKER_SRC_ASSOCIATION_HPP_

#include "hailotracker.h"
#include "tracklet.hpp"
#include "expected.hpp"

#include <vector>

namespace hailotracker {

float32_t compute_iou(const hailo_detection_t &det1, const hailo_detection_t &det2);

struct AssociationConfig {
    float32_t iou_threshold;
    float32_t iou_weight;
    bool is_class_aware;
    uint32_t max_tracklets;

    explicit AssociationConfig(const hailo_tracker_config_t &cfg);
};

struct AssociationResult {
    std::vector<std::pair<const hailo_detection_t*, Tracklet*>> matches;
    std::vector<const hailo_detection_t*> unmatched_detections;
    std::vector<Tracklet*> unmatched_tracklets;
};

class Association {
public:
    explicit Association(const AssociationConfig &config);
    ~Association() = default;

    hailopp::Expected<hailopp_status> run(const hailo_detections_t *detections, std::vector<Tracklet> &tracklets);

    const AssociationResult &result() const { return m_result; }

private:
    AssociationConfig m_config;
    AssociationResult m_result;
    std::vector<bool> m_matched_trks;
};

} // namespace hailotracker

#endif // _HAILOTRACKER_SRC_ASSOCIATION_HPP_
