/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file exported_resource_manager.hpp
 * @brief Holds resources that are exported via c-api
 **/

#ifndef _HAILO_EXPORTED_RESOURCE_MANAGER_HPP_
#define _HAILO_EXPORTED_RESOURCE_MANAGER_HPP_

#include "hailo/hailort.h"

#include <unordered_map>

namespace hailort
{

// TODO: Merge ExportedResourceManager and SharedResourceManager (HRT-10317)
template<typename Resource, typename Key, typename Hash = std::hash<Key>>
class ExportedResourceManager final
{
public:
    static hailo_status register_resource(const Resource &resource, const Key &key)
    {
        return get_instance().register_resource_impl(resource, key);
    }

    static Expected<std::reference_wrapper<Resource>> get_resource(const Key &key)
    {
        return get_instance().get_resource_impl(key);
    }

    static hailo_status unregister_resource(const Key &key)
    {
        return get_instance().unregister_resource_impl(key);
    }

private:
    static ExportedResourceManager& get_instance()
    {
        static ExportedResourceManager instance;
        return instance;
    }

    hailo_status register_resource_impl(const Resource &resource, const Key &key)
    {
        std::lock_guard<std::mutex> lock_guard(m_mutex);

        auto it = m_storage.find(key);
        if (it != m_storage.end()) {
            LOGGER__TRACE("There's already a resource registered under key {}", key);
            return HAILO_INVALID_ARGUMENT;
        }

        m_storage[key] = resource;
        return HAILO_SUCCESS;
    }

    Expected<std::reference_wrapper<Resource>> get_resource_impl(const Key &key)
    {
        std::lock_guard<std::mutex> lock_guard(m_mutex);

        auto it = m_storage.find(key);
        if (it == m_storage.end()) {
            LOGGER__TRACE("Key {} not found in resource manager", key);
            return make_unexpected(HAILO_NOT_FOUND);
        }

        return std::ref(it->second);
    }

    hailo_status unregister_resource_impl(const Key &key)
    {
        std::lock_guard<std::mutex> lock_guard(m_mutex);

        auto it = m_storage.find(key);
        if (it == m_storage.end()) {
            LOGGER__TRACE("Key {} not found in resource manager", key);
            return HAILO_NOT_FOUND;
        }

        m_storage.erase(it);
        return HAILO_SUCCESS;
    }

    std::mutex m_mutex;
    std::unordered_map<Key, Resource, Hash> m_storage;
};

} /* namespace hailort */

#endif /* _HAILO_EXPORTED_RESOURCE_MANAGER_HPP_ */
