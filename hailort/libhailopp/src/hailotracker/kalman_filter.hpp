/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file kalman_filter.hpp
 * @brief 2D Kalman Filter implementation (stateless)
 **/

#ifndef _HAILOTRACKER_SRC_KALMAN_FILTER_HPP_
#define _HAILOTRACKER_SRC_KALMAN_FILTER_HPP_

#include "hailotracker.h"
#include "eigen_wrapper.hpp"

namespace hailotracker {

/** Kalman filter state data held by each Tracklet */
struct KalmanState {
    Eigen::Matrix<float32_t, 4, 1> x;  // State: [x, y, vx, vy]
    Eigen::Matrix<float32_t, 4, 4> P;  // Covariance matrix
    
    KalmanState() {
        x.setZero();
        P.setIdentity();
    }
};

/**
 * Stateless Kalman Filter that operates on external KalmanState.
 * A single instance can be used to predict/update multiple tracklets.
 **/
class KalmanFilter {
public:
    KalmanFilter(float32_t std_weight_position, float32_t std_weight_velocity);
    ~KalmanFilter() = default;

    void predict(KalmanState &state) const;
    void update(KalmanState &state, const hailo_detection_t &det) const;
    void init_state(KalmanState &state, const hailo_detection_t &det) const;

private:
    float32_t m_dt;
    Eigen::Matrix<float32_t, 2, 1> m_u;
    float32_t m_std_weight_position;
    float32_t m_std_weight_velocity;

    // Algorithm matrices (shared across all tracklets)
    Eigen::Matrix<float32_t, 4, 4> m_F;  // State transition
    Eigen::Matrix<float32_t, 4, 2> m_B;  // Control
    Eigen::Matrix<float32_t, 2, 4> m_H;  // Measurement
    Eigen::Matrix<float32_t, 4, 2> m_H_transpose;  // H^T (precomputed)
    Eigen::Matrix<float32_t, 4, 4> m_Q;  // Process noise
    Eigen::Matrix<float32_t, 2, 2> m_R;  // Measurement noise
};

} // namespace hailotracker

#endif // _HAILOTRACKER_SRC_KALMAN_FILTER_HPP_
