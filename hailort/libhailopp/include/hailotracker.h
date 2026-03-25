/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailotracker.h
 * @brief C API for HailoRT Object Tracking library.
 *
 * Multi-object tracking API with Kalman filter predictions and association algorithms.
 **/

#ifndef _HAILOTRACKER_H_
#define _HAILOTRACKER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "status.h"
#include "platform.h"
#include "hailo/hailort.h"

/** @defgroup group_hailopp_tracker Multi-Object Tracking API
 *  @{
 */

/** Tracker context handle (opaque pointer) */
typedef struct Tracker *hailo_tracker;

/** Tracklet states */
typedef enum hailo_tracklet_state_e {
    /** Recently added, not yet confirmed */
    HAILO_TRACKLET_STATE_NEW = 0,
    /** Actively tracked with recent matches */
    HAILO_TRACKLET_STATE_TRACKED,
    /** Unmatched for several frames */
    HAILO_TRACKLET_STATE_LOST,
    /** Max enum value to maintain ABI integrity */
    HAILO_TRACKLET_STATE_MAX_ENUM = HAILOPP_STATUS_MAX_ENUM
} hailo_tracklet_state_t;

/** Tracklet information */
typedef struct {
    /** Unique identifier */
    uint32_t id;
    /** Tracklet bounding box and class information */
    hailo_detection_t detection;
    /** Current state */
    hailo_tracklet_state_t state;
    /** Frames without match */
    uint32_t frames_since_update;
    /** Total tracked frames */
    uint32_t total_frames_tracked;
    /** Kalman filter velocity estimate */
    float32_t velocity_x;
    float32_t velocity_y;
} hailo_tracklet_t;

/** Tracklets array */
typedef struct {
    /** Pointer to array of tracklets */
    hailo_tracklet_t *tracklets;
    /** Number of tracklets */
    size_t count;
} hailo_tracklets_t;

/** Tracker configuration parameters */
typedef struct {
    /** Maximum number of tracklets to maintain */
    uint16_t max_tracklets;
    /** Frames before TRACKED->LOST */
    uint8_t max_missed_frames;
    /** Frames before NEW->TRACKED */
    uint8_t min_confirmed_frames;
    /** Frames before LOST->DELETED */
    uint8_t aging_threshold;
    /** Minimum confidence score required to initialize a new tracklet from a detection */
    float32_t add_threshold;
    /** Minimum association score required for associating tracklets */
    float32_t association_threshold;
    /** IoU weight in association scoring */
    float32_t iou_weight;
    /** Enable class filtering for tracklet association*/
    bool class_aware_tracking;
    /** Enable Kalman filter for state estimation */
    bool enable_kalman_filter;
    /** Position noise weight for Kalman filter */
    float32_t position_std_weight;
    /** Velocity noise weight for Kalman filter */
    float32_t velocity_std_weight;
    /** Alpha parameter for EMA box smoothing */
    float32_t smoothing_alpha;
} hailo_tracker_config_t;

/** Default tracker configuration */
#define HAILO_TRACKER_CONFIG_DEFAULT    \
    {                                   \
        100,    /* max_tracklets */     \
        3,      /* max_missed_frames */ \
        3,      /* min_confirmed_frames */ \
        5,      /* aging_threshold */   \
        0.5f,   /* add_threshold */     \
        0.3f,   /* association_threshold */ \
        0.7f,   /* iou_weight */       \
        true,   /* class_aware_tracking */ \
        true,   /* enable_kalman_filter */ \
        0.05f,  /* position_std_weight */ \
        0.1f,   /* velocity_std_weight */ \
        0.7f,   /* smoothing_alpha */  \
    }

/**
 * @brief Create a new tracker
 *
 * @param[in] config Tracker configuration
 * @param[out] tracker Pointer to store tracker handle
 * @return Upon success, returns ::HAILOPP_SUCCESS. Otherwise, returns a ::hailopp_status error.
 */
HAILOPPAPI hailopp_status hailo_tracker_create(const hailo_tracker_config_t *config, hailo_tracker *tracker);
/**
 * @brief Release tracker resources
 *
 * @param[in] tracker Tracker handle
 * @return Upon success, returns ::HAILOPP_SUCCESS. Otherwise, returns a ::hailopp_status error.
 */
HAILOPPAPI hailopp_status hailo_tracker_release(hailo_tracker tracker);

/**
 * @brief Update tracker with new detections
 *
 * @param[in] tracker Tracker handle
 * @param[in] detections New detections (hailort structure)
 * @return Upon success, returns ::HAILOPP_SUCCESS. Otherwise, returns a ::hailopp_status error.
 */
HAILOPPAPI hailopp_status hailo_tracker_update(hailo_tracker tracker, const hailo_detections_t *detections);

/**
 * @brief Perform prediction step and get current tracklets
 *
 * @param[in] tracker Tracker handle
 * @param[out] tracklets Internal, transient storage containing the current tracklet predictions.
 * @return Upon success, returns ::HAILOPP_SUCCESS. Otherwise, returns a ::hailopp_status error.
 *
 * @note The contents will be overwritten by subsequent calls.
 *       Callers MUST copy the data if it needs to be retained.
 */
HAILOPPAPI hailopp_status hailo_tracker_predict(hailo_tracker tracker, hailo_tracklets_t *tracklets);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _HAILOTRACKER_H_ */
