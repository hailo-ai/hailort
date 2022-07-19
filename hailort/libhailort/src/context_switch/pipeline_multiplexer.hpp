/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_multiplexer.hpp
 * @brief The pipeline multiplexer is a synchronization mechanism that allows communication
 *        between different pipelines that use the same low-level streams.
 **/

#ifndef _HAILO_PIPELINE_MULTIPLEXER_HPP_
#define _HAILO_PIPELINE_MULTIPLEXER_HPP_

#include "hailo/event.hpp"
#include "hailo/network_group.hpp"
#include "network_group_scheduler.hpp"

#include <queue>
#include <mutex>

namespace hailort
{

class PipelineMultiplexer
{
public:
    virtual ~PipelineMultiplexer() = default;
    PipelineMultiplexer(const PipelineMultiplexer &other) = delete;
    PipelineMultiplexer &operator=(const PipelineMultiplexer &other) = delete;
    PipelineMultiplexer &operator=(PipelineMultiplexer &&other) = delete;
    PipelineMultiplexer(PipelineMultiplexer &&other) = delete;

    PipelineMultiplexer();

    hailo_status reader_wait(network_group_handle_t network_group_handle, const std::string &network_name, const std::string &output_name);

    hailo_status signal_sent_frame(network_group_handle_t network_group_handle, const std::string &network_name);

    /* This function is called by outputs stream when a frame has been received.
        Only when all outputs of a network have received a frame, the relevant events will be signalled. */
    hailo_status signal_received_frame(network_group_handle_t network_group_handle, const std::string &network_name, const std::string &output_name);

    Expected<std::unique_lock<std::mutex>> acquire_write_lock(network_group_handle_t network_group_handle);

private:
    Expected<EventPtr> get_reader_event(network_group_handle_t network_group_handle, const std::string &network_name,
        const std::string &output_name, bool should_create=false);
    hailo_status signal_next(const std::string &network_name);

    std::mutex m_readers_queue_mutex;
    
    using EventMap = std::unordered_map<std::string, EventPtr>;
    std::unordered_map<std::string, std::queue<std::reference_wrapper<EventMap>>> m_readers_queue_by_network;
    std::unordered_map<std::string, EventMap> m_signalled_events_by_network;

    std::mutex m_write_mutex;

    //map by network_group_handle, network_name, output_name
    std::unordered_map<network_group_handle_t, std::unordered_map<std::string, EventMap>> m_reader_events_mapping;
};

} /* namespace hailort */

#endif /* _HAILO_PIPELINE_MULTIPLEXER_HPP_ */
