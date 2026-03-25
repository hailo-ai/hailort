/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file kalman_filter.cpp
 * @brief 2D Kalman Filter implementation
 **/

#include "kalman_filter.hpp"

namespace hailotracker {

KalmanFilter::KalmanFilter(float32_t std_weight_position, float32_t std_weight_velocity)
    : m_dt(1.0f), m_u(0.0f, 0.0f),
      m_std_weight_position(std_weight_position),
      m_std_weight_velocity(std_weight_velocity)
{
    // Precompute dt powers and sigma (used in multiple places)
    float32_t dt2 = m_dt * m_dt;
    float32_t dt3 = dt2 * m_dt;
    float32_t dt4 = dt2 * dt2;
    float32_t sigma_a2 = m_std_weight_position * m_std_weight_position;
    float32_t half_dt2 = 0.5f * dt2;

    // State transition matrix F
    // [1 0 dt 0]
    // [0 1 0 dt]
    // [0 0 1  0]
    // [0 0 0  1]
    m_F.setIdentity();
    m_F(0, 2) = m_dt;
    m_F(1, 3) = m_dt;

    // Control matrix B
    // [dt^2/2 0]
    // [0 dt^2/2]
    // [dt     0]
    // [0     dt]
    m_B.setZero();
    m_B(0, 0) = half_dt2;
    m_B(1, 1) = half_dt2;
    m_B(2, 0) = m_dt;
    m_B(3, 1) = m_dt;

    // Measurement matrix H
    // [1 0 0 0]
    // [0 1 0 0]
    m_H.setIdentity();
    m_H_transpose = m_H.transpose();

    // Process noise covariance Q
    float32_t q_dt4_coeff = 0.25f * dt4 * sigma_a2;
    float32_t q_dt3_coeff = 0.5f * dt3 * sigma_a2;
    float32_t q_dt2_coeff = dt2 * sigma_a2;

    m_Q.setZero();
    m_Q(0, 0) = q_dt4_coeff;
    m_Q(0, 2) = q_dt3_coeff;
    m_Q(1, 1) = q_dt4_coeff;
    m_Q(1, 3) = q_dt3_coeff;
    m_Q(2, 0) = q_dt3_coeff;
    m_Q(2, 2) = q_dt2_coeff;
    m_Q(3, 1) = q_dt3_coeff;
    m_Q(3, 3) = q_dt2_coeff;

    // Measurement noise covariance R
    float32_t sigma_z = m_std_weight_velocity;
    m_R.setIdentity();
    m_R *= (sigma_z * sigma_z);
}

void KalmanFilter::predict(KalmanState &state) const
{
    // x = F * x + B * u
    state.x = m_F * state.x + m_B * m_u;
    
    // P = F * P * F^T + Q
    state.P = m_F * state.P * m_F.transpose() + m_Q;
}

void KalmanFilter::update(KalmanState &state, const hailo_detection_t &det) const
{
    // Calculate detection center
    float32_t cx = (det.x_min + det.x_max) * 0.5f;
    float32_t cy = (det.y_min + det.y_max) * 0.5f;
    Eigen::Matrix<float32_t, 2, 1> measurement(cx, cy);
    
    // y = z - H * x (Innovation)
    Eigen::Matrix<float32_t, 2, 1> y = measurement - m_H * state.x;

    // Precompute P * H^T (used multiple times)
    Eigen::Matrix<float32_t, 4, 2> PHt = state.P * m_H_transpose;

    // S = H * P * H^T + R (Innovation covariance)
    Eigen::Matrix<float32_t, 2, 2> S = m_H * PHt + m_R;

    // K = P * H^T * S^-1 (Kalman gain)
    Eigen::Matrix<float32_t, 4, 2> K = PHt * S.inverse();

    // x = x + K * y
    state.x = state.x + K * y;

    // P = (I - K * H) * P
    state.P = (Eigen::Matrix<float32_t, 4, 4>::Identity() - K * m_H) * state.P;
}

void KalmanFilter::init_state(KalmanState &state, const hailo_detection_t &det) const
{
    float32_t cx = (det.x_min + det.x_max) * 0.5f;
    float32_t cy = (det.y_min + det.y_max) * 0.5f;
    
    state.x.setZero();
    state.x(0) = cx;
    state.x(1) = cy;
    state.P.setIdentity();
}

} // namespace hailotracker

