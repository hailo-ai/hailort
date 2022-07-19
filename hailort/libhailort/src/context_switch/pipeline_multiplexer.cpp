/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_multiplexer.cpp
 * @brief: Pipeline Multiplexer
 **/

#include "common/utils.hpp"
#include "hailo/hailort_common.hpp"

#include "pipeline_multiplexer.hpp"

namespace hailort
{

PipelineMultiplexer::PipelineMultiplexer() :
    m_readers_queue_mutex(),
    m_readers_queue_by_network(),
    m_signalled_events_by_network(),
    m_write_mutex(),
    m_reader_events_mapping()
{}

Expected<EventPtr> PipelineMultiplexer::get_reader_event(network_group_handle_t network_group_handle, const std::string &network_name,
    const std::string &output_name, bool should_create)
{
    auto iter_by_handle = m_reader_events_mapping.find(network_group_handle);
    if (m_reader_events_mapping.end() == iter_by_handle) {
        if (!should_create) {
            return make_unexpected(HAILO_NOT_FOUND);
        }
        m_reader_events_mapping.emplace(network_group_handle, std::unordered_map<std::string, std::unordered_map<std::string, EventPtr>>());
        iter_by_handle = m_reader_events_mapping.find(network_group_handle);
    }

    auto iter_by_network_name = iter_by_handle->second.find(network_name);
    if (iter_by_handle->second.end() == iter_by_network_name) {
        if (!should_create) {
            return make_unexpected(HAILO_NOT_FOUND);
        }
        iter_by_handle->second.emplace(network_name, std::unordered_map<std::string, EventPtr>());
        iter_by_network_name = iter_by_handle->second.find(network_name);

        std::unique_lock<std::mutex> lock(m_readers_queue_mutex);
        if (!contains(m_readers_queue_by_network, network_name)) {
            m_readers_queue_by_network.emplace(network_name, std::queue<std::reference_wrapper<EventMap>>());
        }
        if (!contains(m_signalled_events_by_network, network_name)) {
            m_signalled_events_by_network.emplace(network_name, EventMap());
        }
    }

    auto event_iter = iter_by_network_name->second.find(output_name);
    if (iter_by_network_name->second.end() == event_iter) {
        if (!should_create) {
            return make_unexpected(HAILO_NOT_FOUND);
        }
        auto read_event = Event::create_shared(Event::State::not_signalled);
        CHECK_AS_EXPECTED(nullptr != read_event, HAILO_OUT_OF_HOST_MEMORY, "Failed to create read event");
        iter_by_network_name->second.emplace(output_name, read_event);
        return read_event;
    }
    return std::move(event_iter->second);
}

hailo_status PipelineMultiplexer::reader_wait(network_group_handle_t network_group_handle, const std::string &network_name, const std::string &output_name)
{
    auto reader_event = get_reader_event(network_group_handle, network_name, output_name);
    if (HAILO_NOT_FOUND == reader_event.status()) {
        return HAILO_SUCCESS;
    }
    CHECK_EXPECTED_AS_STATUS(reader_event);
    return reader_event.value()->wait(HAILO_INFINITE_TIMEOUT);
}

hailo_status PipelineMultiplexer::signal_next(const std::string &network_name)
{
    auto readers_queue_iter = m_readers_queue_by_network.find(network_name);
    CHECK((m_readers_queue_by_network.end() != readers_queue_iter) && !(readers_queue_iter->second.empty()),
        HAILO_INTERNAL_FAILURE, "Failed to signal read events");
    for (auto &output_name_to_event : readers_queue_iter->second.front().get()) {
        auto status = output_name_to_event.second->signal();
        CHECK_SUCCESS(status);
    }

    m_signalled_events_by_network[network_name] = readers_queue_iter->second.front().get();
    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::signal_sent_frame(network_group_handle_t network_group_handle, const std::string &network_name)
{
    auto events_by_network_iter = m_reader_events_mapping.find(network_group_handle);
    if (m_reader_events_mapping.end() == events_by_network_iter) {
        return HAILO_SUCCESS;
    }

    auto network_read_events_iter = events_by_network_iter->second.find(network_name);
    CHECK(events_by_network_iter->second.end() != network_read_events_iter,
        HAILO_NOT_FOUND, "Could not find network name {}", network_name);

    std::unique_lock<std::mutex> lock(m_readers_queue_mutex);
    auto readers_queue_iter = m_readers_queue_by_network.find(network_name);
    CHECK(m_readers_queue_by_network.end() != readers_queue_iter,
        HAILO_NOT_FOUND, "Could not find multiplexer queue for network name {}", network_name);
    readers_queue_iter->second.push(network_read_events_iter->second);
    if (1 == readers_queue_iter->second.size()) {
        signal_next(network_name);
    }

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::signal_received_frame(network_group_handle_t network_group_handle, const std::string &network_name, const std::string &output_name)
{
    if (!contains(m_reader_events_mapping, network_group_handle)) {
        return HAILO_SUCCESS;
    }

    std::unique_lock<std::mutex> lock(m_readers_queue_mutex);
    
    auto readers_queue_iter = m_readers_queue_by_network.find(network_name);
    CHECK(m_readers_queue_by_network.end() != readers_queue_iter,
        HAILO_NOT_FOUND, "Could not find multiplexer queue for network name {}", network_name);
    CHECK(!readers_queue_iter->second.empty(), HAILO_INTERNAL_FAILURE);

    auto signalled_events_iter = m_signalled_events_by_network.find(network_name);
    CHECK(m_signalled_events_by_network.end() != signalled_events_iter,
        HAILO_NOT_FOUND, "Could not find multiplexer signalled events for network name {}", network_name);
    auto signalled_event_iter = signalled_events_iter->second.find(output_name);
    CHECK(signalled_events_iter->second.end() != signalled_event_iter, HAILO_NOT_FOUND,
        "Could not find signalled event for output name {}", output_name);

    auto status = signalled_event_iter->second->reset();
    CHECK_SUCCESS(status);

    signalled_events_iter->second.erase(output_name);
    if (0 == signalled_events_iter->second.size()) {
        readers_queue_iter->second.pop();
        if (!readers_queue_iter->second.empty()) {
            signal_next(network_name);
        }
    }

    return HAILO_SUCCESS;
}

Expected<std::unique_lock<std::mutex>> PipelineMultiplexer::acquire_write_lock(network_group_handle_t network_group_handle)
{
    if (contains(m_reader_events_mapping, network_group_handle)) {
        return std::unique_lock<std::mutex>(m_write_mutex);
    } else {
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

} /* namespace hailort */
