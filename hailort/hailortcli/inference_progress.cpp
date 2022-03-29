/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inference_progress.cpp
 * @brief Show inference progress
 **/
#include "inference_progress.hpp"
#include "infer_stats_printer.hpp"
#include "common.hpp"

#include <iostream>
#include <iomanip>

InferProgress::InferProgress(ConfiguredNetworkGroup &configured_network_group, const inference_runner_params &params,
    std::chrono::duration<double> print_interval) :
      m_configured_network_group(configured_network_group), m_params(params),
      m_print_interval(print_interval), m_networks_progress(), m_stop(true) {}

void InferProgress::start()
{
    m_stop = false;
    m_print_thread = std::thread([this] () {
        while (!m_stop.load()) {
            print_progress(true);
            std::this_thread::sleep_for(m_print_interval);
        }
    });
}

void InferProgress::finish(bool should_print_progress)
{
    m_stop = true;
    m_print_thread.join();
    if (should_print_progress) {
        print_progress(false);
    }
}

void InferProgress::print_progress(bool should_reset_cursor)
{
    for (auto &network_progress_bar : m_networks_progress) {
        std::cout << network_progress_bar->get_progress_text() << std::endl;
    }
    if (should_reset_cursor) {
        CliCommon::reset_cursor(m_networks_progress.size());
    }
}

InferProgress::~InferProgress()
{
    if (!m_stop.load()) {
        finish(false);
    }
}

Expected<std::shared_ptr<NetworkProgressBar>> InferProgress::create_network_progress_bar(const std::string &network_name)
{
    std::shared_ptr<NetworkProgressBar> network_progress_ber =
        make_shared_nothrow<NetworkProgressBar>(m_configured_network_group, m_params, network_name);
    CHECK_NOT_NULL_AS_EXPECTED(network_progress_ber, HAILO_OUT_OF_HOST_MEMORY);

    {
        // We create NetworkProgressBar from different threads
        std::unique_lock<std::mutex> lock(m_mutex);
        m_networks_progress.push_back(network_progress_ber);
    }

    auto prog_bar_cpy = network_progress_ber;
    return prog_bar_cpy;
}

NetworkProgressBar::NetworkProgressBar(ConfiguredNetworkGroup &configured_network_group,
    const inference_runner_params &params, const std::string &network_name) :
      m_network_name(network_name), m_configured_network_group(configured_network_group), m_params(params),
      m_progress_count(0), m_start(std::chrono::steady_clock::now()) // NetworkProgressBar sets start time to its creation time
      {}

std::string NetworkProgressBar::get_progress_text()
{
    std::stringstream res;
    auto elapsed_time = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
    auto progress_count = m_progress_count.load();
    auto fps = progress_count / elapsed_time;
    auto eta = std::chrono::seconds(0);
    if (0 == m_params.time_to_run) {
        eta = std::chrono::seconds(static_cast<uint32_t>(static_cast<double>(m_params.frames_count - progress_count) / fps));
    } else {
        eta = std::chrono::seconds(std::max(static_cast<int32_t>(0),
            static_cast<int32_t>(std::round(m_params.time_to_run - elapsed_time))));
    }

    // Set precision and flags
    res << std::setprecision(2) << std::fixed;

    uint32_t progress_percent = 0;
    if (0 == m_params.time_to_run) {
        progress_percent = 100 * progress_count / m_params.frames_count;
    } else {
        progress_percent = std::min(static_cast<uint32_t>(100 * elapsed_time / m_params.time_to_run), static_cast<uint32_t>(100));
    }

    res << "Network " << m_network_name << ": " << progress_percent << "% | " << progress_count;
    if (0 == m_params.time_to_run) {
        res << "/" << m_params.frames_count;
    }

    if (!m_params.measure_latency) {
        res << " | FPS: " << fps;
    } else {
        double avg_hw_latency = 0;
        auto latency_expected = m_configured_network_group.get_latency_measurement(m_network_name);
        if (latency_expected) {
            avg_hw_latency = InferResultsFormatUtils::latency_result_to_ms(latency_expected.release().avg_hw_latency);
        }

        if (avg_hw_latency > 0) {
            res << " | HW Latency: " << avg_hw_latency << " ms";
        }
        else {
            res << " | HW Latency: NaN";
        }
    }
    res << " | ETA: " << CliCommon::duration_to_string(eta) << std::flush;

    return res.str();
}

void NetworkProgressBar::make_progress()
{
    ++m_progress_count;
}