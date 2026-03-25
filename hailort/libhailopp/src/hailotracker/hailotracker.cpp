/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
};

extern "C" {

HAILOPPAPI hailopp_status hailo_tracker_create(const hailo_tracker_config_t *config, hailo_tracker *tracker)
{
    CHECK_NOT_NULL(config);
    CHECK_NOT_NULL(tracker);

    auto expected_impl = hailotracker::Tracker::create(*config);
    CHECK_EXPECTED_AS_STATUS(expected_impl);

    auto ctx = new (std::nothrow) ::Tracker();
    CHECK_NOT_NULL(ctx);
    ctx->impl = std::move(expected_impl.value());

    *tracker = ctx;
    return HAILOPP_SUCCESS;
}

HAILOPPAPI hailopp_status hailo_tracker_release(hailo_tracker tracker)
{
    CHECK_NOT_NULL(tracker);

    delete tracker;
    return HAILOPP_SUCCESS;
}

HAILOPPAPI hailopp_status hailo_tracker_update(hailo_tracker tracker, const hailo_detections_t *detections)
{
    CHECK_NOT_NULL(tracker);
    CHECK_NOT_NULL(tracker->impl);
    CHECK_NOT_NULL(detections);

    auto status = tracker->impl->update(detections);
    CHECK_EXPECTED_AS_STATUS(status);
    return HAILOPP_SUCCESS;
}

HAILOPPAPI hailopp_status hailo_tracker_predict(hailo_tracker tracker, hailo_tracklets_t *tracklets)
{
    CHECK_NOT_NULL(tracker);
    CHECK_NOT_NULL(tracklets);
    CHECK_NOT_NULL(tracker->impl);

    auto predict_status = tracker->impl->predict();
    CHECK_EXPECTED_AS_STATUS(predict_status);

    auto &results = tracker->impl->get_current_tracklets();
    tracklets->tracklets = results.data();
    tracklets->count = results.size();
    return HAILOPP_SUCCESS;
}

}
