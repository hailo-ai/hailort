/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file service_resource_manager.hpp
 * @brief manages handles for resource objects.
 *
 **/

#ifndef HAILO_SERVICE_RESOURCE_MANAGER_HPP_
#define HAILO_SERVICE_RESOURCE_MANAGER_HPP_

#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include <mutex>
#include <shared_mutex>

namespace hailort
{

template<class T>
struct Resource {
    Resource(uint32_t pid, std::shared_ptr<T> resource)
        : pid(pid), resource(std::move(resource))
    {}

    uint32_t pid;
    std::shared_ptr<T> resource;
};

template<class T>
class ServiceResourceManager
{
public:
    static ServiceResourceManager& get_instance()
    {
        static ServiceResourceManager instance;
        return instance;
    }

    template<class K, class Func, typename... Args>
    K execute(uint32_t handle, Func &lambda, Args... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto resource_expected = resource_lookup(handle);
        assert(resource_expected);
        auto resource = resource_expected.release();

        assert(contains(m_resources_mutexes, handle));
        std::shared_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
        lock.unlock();
        K ret = lambda(resource->resource, args...);

        return ret;
    }

    uint32_t register_resource(uint32_t pid, const std::shared_ptr<T> &resource)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto index = m_current_handle_index.load();
        // Create a new resource and register
        m_resources.emplace(m_current_handle_index, std::make_shared<Resource<T>>(pid, std::move(resource)));
        m_resources_mutexes[m_current_handle_index]; // construct std::shared_timed_mutex
        m_current_handle_index++;
        return index;
    }

    uint32_t dup_handle(uint32_t pid, uint32_t handle)
    {
        // Keeping this function for future possible usage
        (void)pid;
        return handle;
    }

    hailo_status release_resource(uint32_t handle)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto found = m_resources.find(handle);
        CHECK(found != m_resources.end(), HAILO_NOT_FOUND, "Failed to release resource with handle {}, resource does not exist", handle);
        assert(contains(m_resources_mutexes, handle));
        auto resource = m_resources[handle];
        {
            std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
            m_resources.erase(handle);
        }
        m_resources_mutexes.erase(handle);
        return HAILO_SUCCESS;
    }

    void release_by_pid(uint32_t pid)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto iter = m_resources.begin(); iter != m_resources.end(); ) {
            auto handle = iter->first;
            if (iter->second->pid == pid) {
                assert(contains(m_resources_mutexes, handle));
                {
                    std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
                    iter = m_resources.erase(iter);
                }
                m_resources_mutexes.erase(handle);
            } else {
                ++iter;
            }
        }
    }

private:
    ServiceResourceManager()
        : m_current_handle_index(0)
    {}

    Expected<std::shared_ptr<Resource<T>>> resource_lookup(uint32_t handle)
    {
        auto found = m_resources.find(handle);
        CHECK_AS_EXPECTED(found != m_resources.end(), HAILO_NOT_FOUND, "Failed to find resource with handle {}", handle);
        auto resource = found->second;
        return resource;
    }

    std::mutex m_mutex;
    std::atomic<uint32_t> m_current_handle_index;
    std::unordered_map<uint32_t, std::shared_ptr<Resource<T>>> m_resources;
    std::unordered_map<uint32_t, std::shared_timed_mutex> m_resources_mutexes;
};

}

#endif /* HAILO_SERVICE_RESOURCE_MANAGER_HPP_ */
