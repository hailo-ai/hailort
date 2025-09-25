/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

// TODO: Merge ExportedResourceManager and SharedResourceManager (HRT-10317)
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
            m_resources.at(available_index) = std::make_unique<ResourceRef<Key, T>>(user_key, expected_resource.release());
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
        : m_resources(max_resources())
    {}

    // On graceful process clean, the destructor of this class will be called, and m_resources should be an empty
    // list (since all resources we released). If it is not the case (for example, the user called ExitProcess), we
    // don't want to release the objects - just leak them. It is OK to leak the objects since the user didn't call
    // release_resource (what they expect us to do?).
    // It is important to leak the memory since we may not be able to free the objects when the process is being
    // destructed:
    //    1. On windows for example, the static variables are destroyed *after* the threads stops.
    //       Some shared resources waits for their threads to do something, and they can stack for ever.
    //    2. The object destruction may relay on other singleton object destruction.
    ~SharedResourceManager()
    {
        for (auto &resource : m_resources) {
            // Releasing resource will leak its memory
            resource.release();
        }
    }

    static uint32_t max_resources()
    {
        // This method can be "overriden" with template specialization
        // to set another MAX for specific managers.
        return HAILO_MAX_SHARED_RESOURCES;
    }

    // This method can be "overriden" with template specialization
    // to set another UNIQUE for specific managers.
    static Key unique_key()
    {
        if (std::is_same<Key, std::string>::value) {
            // Special case for when Key is std::string to prevent calling std::string(0)
            return HAILO_UNIQUE_VDEVICE_GROUP_ID;
        } else {
            return HAILO_UNIQUE_RESOURCE_KEY;
        }
    }

    std::mutex m_mutex;
    std::vector<std::unique_ptr<ResourceRef<Key, T>>> m_resources;
};

}

#endif /* HAILO_SHARED_RESOURCE_MANAGER_HPP_ */
