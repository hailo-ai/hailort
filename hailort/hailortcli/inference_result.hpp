/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inference_result.hpp
 * @brief hold inference result
 **/

#ifndef _HAILO_INFER_RESULT_
#define _HAILO_INFER_RESULT_

#include "power_measurement_command.hpp"
#include "common/device_measurements.hpp"
#include "hailo/runtime_statistics.hpp"
#include "hailo/vstream.hpp"

static constexpr double MBIT_PER_BYTE = 8.0f / 1000.0f / 1000.0f;

struct NetworkInferResult
{
public:
    NetworkInferResult(size_t frames_count = 0, size_t total_send_frame_size = 0, size_t total_recv_frame_size = 0) :
        m_frames_count(frames_count),
        m_total_send_frame_size(total_send_frame_size),
        m_total_recv_frame_size(total_recv_frame_size),
        m_infer_duration(nullptr),
        m_hw_latency(nullptr),
        m_overall_latency(nullptr)
    {}

    Expected<double> infer_duration() const{
        if (!m_infer_duration) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        auto infer_duration_cpy = *m_infer_duration;
        return infer_duration_cpy;
    }

    Expected<double> fps() const
    {
        if (!m_infer_duration) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        return static_cast<double>(m_frames_count) / *m_infer_duration;
    }

    Expected<double> send_data_rate_mbit_sec() const
    {
        if (!m_infer_duration || !m_total_send_frame_size) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
       return (static_cast<double>(m_frames_count * m_total_send_frame_size) / *m_infer_duration) * MBIT_PER_BYTE;
    }

    Expected<double> recv_data_rate_mbit_sec() const
    {
        if (!m_infer_duration || !m_total_recv_frame_size) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        return (static_cast<double>(m_frames_count * m_total_recv_frame_size) / *m_infer_duration) * MBIT_PER_BYTE;
    }

    Expected<std::chrono::nanoseconds> hw_latency() const
    {
        if (!m_hw_latency) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        std::chrono::nanoseconds latency_cpy = *m_hw_latency;
        return latency_cpy;
    }

    Expected<std::chrono::nanoseconds> overall_latency() const
    {
        if (!m_overall_latency) {
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        std::chrono::nanoseconds latency_cpy = *m_overall_latency;
        return latency_cpy;
    }

    size_t m_frames_count;
    size_t m_total_send_frame_size;
    size_t m_total_recv_frame_size;

    // TODO: change to optional
    std::shared_ptr<double> m_infer_duration;
    std::shared_ptr<std::chrono::nanoseconds> m_hw_latency;
    std::shared_ptr<std::chrono::nanoseconds> m_overall_latency;
};

struct NetworkGroupInferResult
{
public:
    NetworkGroupInferResult(const std::string &network_group_name, std::map<std::string, NetworkInferResult> &&result_per_network = {}) :
        m_network_group_name(network_group_name),
        m_result_per_network(std::move(result_per_network)),
        m_fps_accumulators(),
        m_latency_accumulators(),
        m_queue_size_accumulators(),
        m_pipeline_latency_accumulators()
    {}

    std::string network_group_name()
    {
        return m_network_group_name;
    }

    Expected<double> infer_duration(const std::string &network_name = "") const
    {
        if (network_name.empty()) {
            // We set m_infer_duration to be the max of all durations
            double max_duration = 0;
            for (auto &network_result_pair : m_result_per_network) {
                auto duration_per_network = network_result_pair.second.infer_duration();
                if(!duration_per_network) {
                    return make_unexpected(HAILO_NOT_AVAILABLE);
                }
                max_duration = std::max(max_duration, duration_per_network.value());
            }
            return max_duration;
        }
        CHECK_AS_EXPECTED(contains(m_result_per_network, network_name), HAILO_NOT_FOUND,
            "There is no results for network {}", network_name);
        return m_result_per_network.at(network_name).infer_duration();
    }

    Expected<double> fps(const std::string &network_name = "") const
    {
        if (network_name.empty()) {
            double acc_fps = 0;
            for (auto &network_result_pair : m_result_per_network) {
                auto fps_per_network = network_result_pair.second.fps();
                if (!fps_per_network) {
                    return make_unexpected(HAILO_NOT_AVAILABLE);
                }
                acc_fps += fps_per_network.value();
            }
            return (acc_fps / static_cast<double>(m_result_per_network.size()));
        }
        CHECK_AS_EXPECTED(contains(m_result_per_network, network_name), HAILO_NOT_FOUND,
            "There is no results for network {}", network_name);
        return m_result_per_network.at(network_name).fps();
    }

    Expected<double> send_data_rate_mbit_sec(const std::string &network_name = "") const
    {
        if (network_name.empty()) {
            double acc_send_data_rate_mbit_sec = 0;
            for (auto &network_result_pair : m_result_per_network) {
                auto send_data_rate_mbit_sec_per_network = network_result_pair.second.send_data_rate_mbit_sec();
                if(!send_data_rate_mbit_sec_per_network) {
                    return make_unexpected(HAILO_NOT_AVAILABLE);
                }
                acc_send_data_rate_mbit_sec += send_data_rate_mbit_sec_per_network.value();
            }
            return acc_send_data_rate_mbit_sec;
        }
        CHECK_AS_EXPECTED(contains(m_result_per_network, network_name), HAILO_NOT_FOUND,
            "There is no results for network {}", network_name);
        return m_result_per_network.at(network_name).send_data_rate_mbit_sec();
    }

    Expected<double> recv_data_rate_mbit_sec(const std::string &network_name = "") const
    {
        if (network_name.empty()) {
            double acc_recv_data_rate_mbit_sec = 0;
            for (auto &network_result_pair : m_result_per_network) {
                auto recv_data_rate_mbit_sec_per_network = network_result_pair.second.recv_data_rate_mbit_sec();
                if(!recv_data_rate_mbit_sec_per_network) {
                    return make_unexpected(HAILO_NOT_AVAILABLE);
                }
                acc_recv_data_rate_mbit_sec += recv_data_rate_mbit_sec_per_network.value();
            }
            return acc_recv_data_rate_mbit_sec;
        }
        CHECK_AS_EXPECTED(contains(m_result_per_network, network_name), HAILO_NOT_FOUND,
            "There is no results for network {}", network_name);
        return m_result_per_network.at(network_name).send_data_rate_mbit_sec();
    }

    Expected<std::chrono::nanoseconds> hw_latency(const std::string &network_name = "") const
    {
        if (network_name.empty()) {
            std::chrono::nanoseconds acc_hw_latency(0);
            for (auto &network_result_pair : m_result_per_network) {
                auto hw_latency_per_network = network_result_pair.second.hw_latency();
                if(!hw_latency_per_network) {
                    return make_unexpected(HAILO_NOT_AVAILABLE);
                }
                acc_hw_latency += hw_latency_per_network.value();
            }
            return static_cast<std::chrono::nanoseconds>(acc_hw_latency / m_result_per_network.size());
        }
        CHECK_AS_EXPECTED(contains(m_result_per_network, network_name), HAILO_NOT_FOUND,
            "There is no results for network {}", network_name);
        return m_result_per_network.at(network_name).hw_latency();
    }

    Expected<std::chrono::nanoseconds> overall_latency(const std::string &network_name = "") const
    {
        if (network_name.empty()) {
            std::chrono::nanoseconds acc_overall_latency(0);
            for (auto &network_result_pair : m_result_per_network) {
                auto overall_latency_per_network = network_result_pair.second.overall_latency();
                if(!overall_latency_per_network) {
                    return make_unexpected(HAILO_NOT_AVAILABLE);
                }
                acc_overall_latency += overall_latency_per_network.value();
            }
            return static_cast<std::chrono::nanoseconds>(acc_overall_latency / m_result_per_network.size());
        }
        CHECK_AS_EXPECTED(contains(m_result_per_network, network_name), HAILO_NOT_FOUND,
            "There is no results for network {}", network_name);
        return m_result_per_network.at(network_name).overall_latency();
    }

    Expected<size_t> frames_count(const std::string &network_name = "") const
    {
        if (network_name.empty()) {
            if (1 == m_result_per_network.size()) {
                // If there is only one network, return its frames_count
                auto frames_count_cpy = m_result_per_network.begin()->second.m_frames_count;
                return frames_count_cpy;
            }
            return make_unexpected(HAILO_NOT_AVAILABLE);
        }
        CHECK_AS_EXPECTED(contains(m_result_per_network, network_name), HAILO_NOT_FOUND,
            "There is no results for network {}", network_name);
        auto frames_count_cpy =  m_result_per_network.at(network_name).m_frames_count;
        return frames_count_cpy;
    }

    const std::map<std::string, NetworkInferResult> &results_per_network() const
    {
        return m_result_per_network;
    }

    std::string m_network_group_name;
    // <network_name, network_inference_results>
    std::map<std::string, NetworkInferResult> m_result_per_network;

    // <vstream_name, accumulator>
    std::map<std::string, std::map<std::string, AccumulatorPtr>> m_fps_accumulators;
    std::map<std::string, std::map<std::string, AccumulatorPtr>> m_latency_accumulators;
    std::map<std::string, std::map<std::string, std::vector<AccumulatorPtr>>> m_queue_size_accumulators;
    std::map<std::string, AccumulatorPtr> m_pipeline_latency_accumulators;

    void update_pipeline_stats(const std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>> &inputs_per_network,
        const std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>> &outputs_per_network)
    {
        for (const auto &inputs_pair : inputs_per_network) {
            for (const auto &in_vstream : inputs_pair.second) {
                update_accumulator_map(m_fps_accumulators, in_vstream.get().name(), in_vstream.get().get_fps_accumulators());
                update_accumulator_map(m_latency_accumulators, in_vstream.get().name(), in_vstream.get().get_latency_accumulators());
                update_accumulator_map(m_queue_size_accumulators, in_vstream.get().name(), in_vstream.get().get_queue_size_accumulators());
                const auto pipeline_latency_accumulator = in_vstream.get().get_pipeline_latency_accumulator();
                if (nullptr != pipeline_latency_accumulator) {
                    m_pipeline_latency_accumulators.emplace(in_vstream.get().name(), pipeline_latency_accumulator);
                }
            }
        }

        for (const auto &outputs_pair : outputs_per_network) {
            for (const auto &out_vstream : outputs_pair.second) {
                update_accumulator_map(m_fps_accumulators, out_vstream.get().name(), out_vstream.get().get_fps_accumulators());
                update_accumulator_map(m_latency_accumulators, out_vstream.get().name(), out_vstream.get().get_latency_accumulators());
                update_accumulator_map(m_queue_size_accumulators, out_vstream.get().name(), out_vstream.get().get_queue_size_accumulators());
                const auto pipeline_latency_accumulator = out_vstream.get().get_pipeline_latency_accumulator();
                if (nullptr != pipeline_latency_accumulator) {
                    m_pipeline_latency_accumulators.emplace(out_vstream.get().name(), pipeline_latency_accumulator);
                }
            }
        }
    }

    void update_pipeline_stats(const std::map<std::string, std::vector<std::reference_wrapper<InputStream>>> &/*inputs_per_network*/,
        const std::map<std::string, std::vector<std::reference_wrapper<OutputStream>>> &/*outputs_per_network*/)
    {
        // Overloading fow hw_only inference - not using any pipelines so nothing to update
    }

private:
    template <typename K, typename V>
    static void update_accumulator_map(std::map<K, V> &map,
        const K &key, const V &value)
    {
        if (value.size() == 0) {
            return;
        }

        map.emplace(key, value);
    }
};

struct InferResult
{
public:
    InferResult(std::vector<NetworkGroupInferResult> &&network_groups_results) : m_network_group_results(std::move(network_groups_results))
        {}

    std::vector<NetworkGroupInferResult> &network_group_results()
    {
        return m_network_group_results;
    }

    void initialize_measurements(const std::vector<std::reference_wrapper<Device>> &devices)
    {
        for (const auto &device : devices) {
            m_power_measurements.emplace(device.get().get_dev_id(), std::shared_ptr<LongPowerMeasurement>{});
            m_current_measurements.emplace(device.get().get_dev_id(), std::shared_ptr<LongPowerMeasurement>{});
            m_temp_measurements.emplace(device.get().get_dev_id(), std::shared_ptr<AccumulatorResults>{});
        }
    }

    hailo_status set_power_measurement(const std::string &device_id, std::shared_ptr<LongPowerMeasurement> &&power_measure)
    {
        auto iter = m_power_measurements.find(device_id);
        CHECK(m_power_measurements.end() != iter, HAILO_INVALID_ARGUMENT);
        iter->second = std::move(power_measure);
        return HAILO_SUCCESS;
    }

    hailo_status set_current_measurement(const std::string &device_id, std::shared_ptr<LongPowerMeasurement> &&current_measure)
    {
        auto iter = m_current_measurements.find(device_id);
        CHECK(m_current_measurements.end() != iter, HAILO_INVALID_ARGUMENT);
        iter->second = std::move(current_measure);
        return HAILO_SUCCESS;
    }

    hailo_status set_temp_measurement(const std::string &device_id, std::shared_ptr<AccumulatorResults> &&temp_measure)
    {
        auto iter = m_temp_measurements.find(device_id);
        CHECK(m_temp_measurements.end() != iter, HAILO_INVALID_ARGUMENT);
        iter->second = std::move(temp_measure);
        return HAILO_SUCCESS;
    }

    // <device_id, measurement>
    // TODO: create a struct containing all device measurements, and keep only one map
    std::map<std::string, std::shared_ptr<LongPowerMeasurement>> m_power_measurements;
    std::map<std::string, std::shared_ptr<LongPowerMeasurement>> m_current_measurements;
    std::map<std::string, std::shared_ptr<AccumulatorResults>> m_temp_measurements;
    bool power_measurements_are_valid = false;

private:
    std::vector<NetworkGroupInferResult> m_network_group_results;
};

#endif /* _HAILO_INFER_RESULT_ */
