/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_runner.hpp
 * @brief Run network on hailo device
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_NETWORK_RUNNER_HPP_
#define _HAILO_HAILORTCLI_RUN2_NETWORK_RUNNER_HPP_

#include "hailo/vdevice.hpp"
#include "hailo/vstream.hpp"
#include "hailo/event.hpp"
#include "hailo/network_group.hpp"
#include "hailo/expected.hpp"

#include "live_printer.hpp"
#include "network_live_track.hpp"

#include <string>
#include <vector>

constexpr uint32_t UNLIMITED_FRAMERATE = 0;

struct VStreamParams
{
    VStreamParams();

    std::string name;
    hailo_vstream_params_t params;
};

struct NetworkParams
{
    std::string hef_path;
    std::string net_group_name;
    std::vector<VStreamParams> vstream_params;

    // Network parameters
    uint16_t batch_size;
    uint32_t scheduler_threshold;
    uint32_t scheduler_timeout_ms;

    // Run parameters
    uint32_t framerate;
};

class NetworkRunner
{
public:
    NetworkRunner(const NetworkParams &params, const std::string &name,
        std::vector<hailort::InputVStream> &&input_vstreams, std::vector<hailort::OutputVStream> &&output_vstreams);
    static hailort::Expected<std::shared_ptr<NetworkRunner>> create_shared(hailort::VDevice &vdevice, const NetworkParams &params);
    hailo_status run(hailort::Event &shutdown_event, LivePrinter &live_printer);
    void stop();

private:
    static hailort::Expected<std::pair<std::vector<hailort::InputVStream>, std::vector<hailort::OutputVStream>>> create_vstreams(
        hailort::ConfiguredNetworkGroup &net_group, const std::map<std::string, hailo_vstream_params_t> &params);
    hailo_status run_input_vstream(hailort::InputVStream &vstream);
    static hailo_status run_output_vstream(hailort::OutputVStream &vstream, bool first, std::shared_ptr<NetworkLiveTrack> net_live_track);


    const NetworkParams &m_params;//TODO: copy instead of ref?
    std::string m_name;
    std::vector<hailort::InputVStream> m_input_vstreams;
    std::vector<hailort::OutputVStream> m_output_vstreams;
};

#endif /* _HAILO_HAILORTCLI_RUN2_NETWORK_RUNNER_HPP_ */