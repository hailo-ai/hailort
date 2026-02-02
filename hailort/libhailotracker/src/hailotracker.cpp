/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailotracker.cpp
 * @brief C API Implementation
 **/

#include "hailotracker.h"
#include "tracker_core.hpp"
#include "utils.hpp"

#include <cstring>
#include <memory>
#include <new>

struct Tracker {
    std::unique_ptr<hailotracker::Tracker> impl;

    // Constructor to initialize impl
    explicit Tracker(const hailo_tracker_config_t &config)
        : impl(std::make_unique<hailotracker::Tracker>(config))
    {
    }
};

extern "C" {

HAILORTAPI hailo_status hailo_tracker_create(const hailo_tracker_config_t *config, hailo_tracker *tracker)
{
    CHECK_NOT_NULL(config);
    CHECK_NOT_NULL(tracker);

    auto ctx = new (std::nothrow) ::Tracker(*config);
    CHECK_NOT_NULL(ctx);
    CHECK_NOT_NULL(ctx->impl);
    
    *tracker = ctx; 
    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_tracker_release(hailo_tracker tracker)
{
    CHECK_NOT_NULL(tracker);

    delete tracker;
    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_tracker_update(hailo_tracker tracker, const hailo_detections_t *detections)
{
    CHECK_NOT_NULL(tracker);
    CHECK_NOT_NULL(tracker->impl);
    CHECK_NOT_NULL(detections);

    tracker->impl->update(detections);
    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_tracker_predict(hailo_tracker tracker, hailo_tracklets_t *tracklets)
{
    CHECK_NOT_NULL(tracker);
    CHECK_NOT_NULL(tracklets);
    CHECK_NOT_NULL(tracker->impl);

    tracker->impl->predict();
    auto &results = tracker->impl->get_current_tracklets();
    tracklets->tracklets = results.data();
    tracklets->count = results.size();
    return HAILO_SUCCESS;
}

}
