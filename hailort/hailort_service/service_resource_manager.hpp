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
    std::shared_timed_mutex resource_mutex;
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
    K execute(uint32_t key, Func &lambda, Args... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto resource_expected = resource_lookup(key);
        assert(resource_expected);

        auto resource = resource_expected.release();
        std::shared_lock<std::shared_timed_mutex> resource_lock(resource->resource_mutex);
        lock.unlock();
        K ret = lambda(resource->resource, args...);

        return ret;
    }

    uint32_t register_resource(uint32_t pid, std::shared_ptr<T> const &resource)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // Create a new resource and register
        auto index = m_current_handle_index;
        m_resources.emplace(m_current_handle_index++, std::make_shared<Resource<T>>(pid, std::move(resource)));
        return index;
    }

    hailo_status release_resource(uint32_t key)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto found = m_resources.find(key);
        CHECK(found != m_resources.end(), HAILO_NOT_FOUND, "Failed to release resource with key {}, resource does not exist", key);
        std::unique_lock<std::shared_timed_mutex> resource_lock(found->second->resource_mutex);
        m_resources.erase(key);
        return HAILO_SUCCESS;
    }

    void release_by_pid(uint32_t pid)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto iter = m_resources.begin(); iter != m_resources.end(); ) {
            if (iter->second->pid == pid) {
                std::unique_lock<std::shared_timed_mutex> resource_lock(iter->second->resource_mutex);
                iter = m_resources.erase(iter);
            } else {
                ++iter;
            }
        }
    }

private:
    ServiceResourceManager()
        : m_current_handle_index(0)
    {}

    Expected<std::shared_ptr<Resource<T>>> resource_lookup(uint32_t key)
    {
        auto found = m_resources.find(key);
        CHECK_AS_EXPECTED(found != m_resources.end(), HAILO_NOT_FOUND, "Failed to find resource with key {}", key);

        auto resource = found->second;
        return resource;
    }

    std::mutex m_mutex;
    uint32_t m_current_handle_index;
    std::unordered_map<uint32_t, std::shared_ptr<Resource<T>>> m_resources;
};

}

#endif /* HAILO_SERVICE_RESOURCE_MANAGER_HPP_ */
