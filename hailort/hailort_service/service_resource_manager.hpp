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
#include "common/os_utils.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_set>

namespace hailort
{

template<class T>
struct Resource {
    Resource(uint32_t pid, std::shared_ptr<T> resource)
        : resource(std::move(resource))
    {
        pids.insert(pid);
    }

    std::shared_ptr<T> resource;
    std::unordered_set<uint32_t> pids;
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

    uint32_t dup_handle(uint32_t handle, uint32_t pid)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto resource_expected = resource_lookup(handle);
        assert(resource_expected);
        auto resource = resource_expected.release();

        assert(contains(m_resources_mutexes, handle));
        std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
        resource->pids.insert(pid);

        return handle;
    }

    std::shared_ptr<T> release_resource(uint32_t handle, uint32_t pid)
    {
        std::shared_ptr<T> res = nullptr;
        std::unique_lock<std::mutex> lock(m_mutex);
        auto found = m_resources.find(handle);
        if (found == m_resources.end()) {
            LOGGER__INFO("Failed to release resource with handle {} and PID {}. The resource no longer exists or may have already been released",
                handle, pid);
            return res;
        }

        assert(contains(m_resources_mutexes, handle));
        auto resource = m_resources[handle];
        bool release_resource = false;
        {
            std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
            resource->pids.erase(pid);
            if (all_pids_dead(resource)) {
                release_resource = true;
                res = resource->resource;
                m_resources.erase(handle);
            }
        }
        if (release_resource) {
            m_resources_mutexes.erase(handle);
        }
        return res;
    }

    std::vector<std::shared_ptr<T>> release_by_pid(uint32_t pid)
    {
        std::vector<std::shared_ptr<T>> res;
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto iter = m_resources.begin(); iter != m_resources.end(); ) {
            auto handle = iter->first;
            bool release_resource = false;
            if (contains(iter->second->pids, pid)) {
                assert(contains(m_resources_mutexes, handle));
                {
                    std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
                    iter->second->pids.erase(pid);
                    if (iter->second->pids.empty()) {
                        release_resource = true;
                        res.push_back(iter->second->resource);
                        iter = m_resources.erase(iter);
                    }
                }
            }
            if (release_resource) {
                m_resources_mutexes.erase(handle);
            } else {
                ++iter;
            }
        }

        return res;
    }

    std::vector<uint32_t> resources_handles_by_pids(std::set<uint32_t> &pids)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        std::vector<uint32_t> resources_handles;
        for (auto &handle_resource_pair : m_resources) {
            for (auto &pid : pids) {
                if (contains(handle_resource_pair.second->pids, pid)) {
                    resources_handles.emplace_back(handle_resource_pair.first);
                }
            }
        }
        return resources_handles;
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

    bool all_pids_dead(std::shared_ptr<Resource<T>> resource)
    {
        for (auto &pid : resource->pids) {
            if (OsUtils::is_pid_alive(pid)) {
                return false;
            }
        }
        return true;
    }

    std::mutex m_mutex;
    std::atomic<uint32_t> m_current_handle_index;
    std::unordered_map<uint32_t, std::shared_ptr<Resource<T>>> m_resources;
    std::unordered_map<uint32_t, std::shared_timed_mutex> m_resources_mutexes;
};

}

#endif /* HAILO_SERVICE_RESOURCE_MANAGER_HPP_ */
