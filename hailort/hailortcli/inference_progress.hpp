/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inference_progress.hpp
 * @brief Show inference progress
 **/

#ifndef _HAILO_INFERENCE_PROGRESS_HPP_
#define _HAILO_INFERENCE_PROGRESS_HPP_

#include "hailortcli.hpp"
#include "run_command.hpp"

#include "hailo/network_group.hpp"
#include "CLI/CLI.hpp"


class NetworkProgressBar final {
public:
    NetworkProgressBar(ConfiguredNetworkGroup &configured_network_group,
        const inference_runner_params &params, const std::string &network_name);

    void make_progress();
    std::string get_progress_text();
private:
    const std::string m_network_name;
    ConfiguredNetworkGroup &m_configured_network_group;
    const inference_runner_params m_params;
    std::atomic<uint32_t> m_progress_count;
    std::chrono::time_point<std::chrono::steady_clock> m_start;
};

class InferProgress final {
public:
    InferProgress(ConfiguredNetworkGroup &configured_network_group,
        const inference_runner_params &params, std::chrono::duration<double> print_interval);

    ~InferProgress();

    Expected<std::shared_ptr<NetworkProgressBar>> create_network_progress_bar(const std::string &network_name);
    void start();
    void finish(bool should_print_progress = true);

private:
    void print_progress(bool should_reset_cursor);

    ConfiguredNetworkGroup &m_configured_network_group;
    const inference_runner_params m_params;
    std::chrono::duration<double> m_print_interval;
    std::vector<std::shared_ptr<NetworkProgressBar>> m_networks_progress;
    std::atomic_bool m_stop;
    std::thread m_print_thread;
    std::mutex m_mutex;
};

#endif /* _HAILO_INFERENCE_PROGRESS_HPP_ */