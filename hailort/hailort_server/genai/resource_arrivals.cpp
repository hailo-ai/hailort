/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file resource_arrivals.cpp
 * @brief Name-keyed arrival tracking of HEF resources
 **/

#include "resource_arrivals.hpp"

#include "common/utils.hpp"
#include "common/event_internal.hpp"

namespace hailort
{
namespace genai
{

Expected<std::shared_ptr<ResourceArrivals>> ResourceArrivals::create(const std::vector<std::string> &resource_names)
{
    return create(resource_names, Event::State::not_signalled);
}

Expected<std::shared_ptr<ResourceArrivals>> ResourceArrivals::create_all_arrived(
    const std::vector<std::string> &resource_names)
{
    return create(resource_names, Event::State::signalled);
}

Expected<std::shared_ptr<ResourceArrivals>> ResourceArrivals::create(const std::vector<std::string> &resource_names,
    Event::State initial_state)
{
    std::unordered_map<std::string, EventPtr> arrival_events;
    for (const auto &resource_name : resource_names) {
        TRY(auto event, Event::create_shared(initial_state));
        const auto is_new_name = arrival_events.emplace(resource_name, event).second;
        CHECK(is_new_name, HAILO_INVALID_ARGUMENT, "HEF resource '{}' is listed more than once", resource_name);
    }

    auto resource_arrivals = std::shared_ptr<ResourceArrivals>(
        new (std::nothrow) ResourceArrivals(std::move(arrival_events)));
    CHECK_NOT_NULL(resource_arrivals, HAILO_OUT_OF_HOST_MEMORY);
    return resource_arrivals;
}

ResourceArrivals::ResourceArrivals(std::unordered_map<std::string, EventPtr> &&arrival_events) :
    m_arrival_events(std::move(arrival_events))
{}

Expected<EventPtr> ResourceArrivals::find_event(const std::string &resource_name) const
{
    const auto event_it = m_arrival_events.find(resource_name);
    CHECK(m_arrival_events.end() != event_it, HAILO_NOT_FOUND,
        "HEF resource '{}' is not part of the HEF being loaded", resource_name);
    return EventPtr(event_it->second);
}

hailo_status ResourceArrivals::mark_arrived(const std::string &resource_name)
{
    TRY(const auto event, find_event(resource_name));
    return event->signal();
}

hailo_status ResourceArrivals::wait_for(const std::string &resource_name, std::chrono::milliseconds timeout,
    EventPtr shutdown_event)
{
    TRY(const auto event, find_event(resource_name));
    const auto status = WaitOrShutdown(event, shutdown_event).wait(timeout);
    CHECK(HAILO_TIMEOUT != status, status, "HEF resource '{}' did not arrive within {} ms", resource_name,
        timeout.count());
    return status;
}

hailo_status ResourceArrivals::check_all_arrived() const
{
    for (const auto &name_and_event : m_arrival_events) {
        const auto status = name_and_event.second->wait(std::chrono::milliseconds(0));
        CHECK(HAILO_TIMEOUT != status, HAILO_INTERNAL_FAILURE, "HEF resource '{}' never arrived", name_and_event.first);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

} /* namespace genai */
} /* namespace hailort */
