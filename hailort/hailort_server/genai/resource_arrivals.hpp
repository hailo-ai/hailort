/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file resource_arrivals.hpp
 * @brief Name-keyed arrival tracking of HEF resources
 **/

#ifndef _HAILO_GENAI_RESOURCE_ARRIVALS_HPP_
#define _HAILO_GENAI_RESOURCE_ARRIVALS_HPP_

#include "hailo/hailort.h"
#include "hailo/event.hpp"
#include "hailo/expected.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace hailort
{
namespace genai
{

enum class HefSource { Local, Streamed };

// Lets each create-path reader wait for exactly the HEF resources it reads, in whatever order they arrive.
class ResourceArrivals final
{
public:
    static Expected<std::shared_ptr<ResourceArrivals>> create(const std::vector<std::string> &resource_names);
    static Expected<std::shared_ptr<ResourceArrivals>> create_all_arrived(
        const std::vector<std::string> &resource_names);

    ResourceArrivals(const ResourceArrivals &) = delete;
    ResourceArrivals &operator=(const ResourceArrivals &) = delete;
    ResourceArrivals(ResourceArrivals &&) = delete;
    ResourceArrivals &operator=(ResourceArrivals &&) = delete;

    hailo_status mark_arrived(const std::string &resource_name);
    hailo_status wait_for(const std::string &resource_name, std::chrono::milliseconds timeout, EventPtr shutdown_event);
    hailo_status check_all_arrived() const;

private:
    static Expected<std::shared_ptr<ResourceArrivals>> create(const std::vector<std::string> &resource_names,
        Event::State initial_state);
    explicit ResourceArrivals(std::unordered_map<std::string, EventPtr> &&arrival_events);

    Expected<EventPtr> find_event(const std::string &resource_name) const;

    const std::unordered_map<std::string, EventPtr> m_arrival_events;
};
using ResourceArrivalsPtr = std::shared_ptr<ResourceArrivals>;

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_RESOURCE_ARRIVALS_HPP_ */
