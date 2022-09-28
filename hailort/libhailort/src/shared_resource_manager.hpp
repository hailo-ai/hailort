/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file shared_resource_manager.hpp
 * @brief holds and manages shared resource objects mapped by a key.
 *
 **/

#ifndef HAILO_SHARED_RESOURCE_MANAGER_HPP_
#define HAILO_SHARED_RESOURCE_MANAGER_HPP_

#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <typeinfo>

namespace hailort
{

#define HAILO_MAX_SHARED_RESOURCES (32)
#define HAILO_UNIQUE_RESOURCE_KEY (0)

template<class Key, class T>
struct ResourceRef {
    ResourceRef(Key user_key, std::shared_ptr<T> resource)
        : user_key(user_key), count(0), resource(std::move(resource))
    {}

    Key user_key;
    uint32_t count;
    std::shared_ptr<T> resource;
};

template<class Key, class T>
class SharedResourceManager
{
public:
    static SharedResourceManager& get_instance()
    {
        static SharedResourceManager instance;
        return instance;
    }

    Expected<std::shared_ptr<T>> resource_lookup(uint32_t handle)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto resource = m_resources.at(handle)->resource;
        return resource;
    }

    template<class CreateFunc>
    Expected<uint32_t> register_resource(Key user_key, CreateFunc create)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        uint32_t available_index = static_cast<uint32_t>(m_resources.size());
        uint32_t match_index = static_cast<uint32_t>(m_resources.size());
        for (uint32_t i = 0; i < m_resources.size(); ++i) {
            if (m_resources.at(i) == nullptr) {
                available_index = i;
            } else {
                if (m_resources.at(i)->user_key == user_key) {
                    // Resource already registered
                    match_index = i;
                    break;
                }
            }
        }
        bool should_create = match_index == m_resources.size() || user_key == unique_key();
        CHECK_AS_EXPECTED(available_index < m_resources.size() || !should_create, HAILO_NOT_AVAILABLE,
            "Tried to create more than {} shared resources of type {}", max_resources(), typeid(T).name());
        if (should_create) {
            // Create a new resource and register
            auto expected_resource = create();
            CHECK_EXPECTED(expected_resource);
            m_resources.at(available_index) = std::make_shared<ResourceRef<Key, T>>(user_key, expected_resource.release());
            m_resources.at(available_index)->count++;
            return available_index;
        }
        m_resources.at(match_index)->count++;
        return match_index;
    }

    void release_resource(uint32_t handle)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_resources.at(handle)->count--;
        if (!m_resources.at(handle)->count) {
            m_resources.at(handle) = nullptr;
        }
    }

private:
    SharedResourceManager()
        : m_resources(max_resources(), nullptr)
    {}

    static uint32_t max_resources()
    {
        // This method can be "overriden" with template specialization
        // to set another MAX for specific managers.
        return HAILO_MAX_SHARED_RESOURCES;
    }

    static Key unique_key()
    {
        // This method can be "overriden" with template specialization
        // to set another UNIQUE for specific managers.
        return HAILO_UNIQUE_RESOURCE_KEY;
    }

    std::mutex m_mutex;
    std::vector<std::shared_ptr<ResourceRef<Key, T>>> m_resources;
};

}

#endif /* HAILO_SHARED_RESOURCE_MANAGER_HPP_ */
