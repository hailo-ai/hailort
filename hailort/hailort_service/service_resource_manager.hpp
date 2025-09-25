/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
    Resource(uint32_t id, std::shared_ptr<T> resource)
        : resource(std::move(resource))
    {
        ids.insert(id);
    }

    std::shared_ptr<T> resource;
    std::unordered_set<uint32_t> ids;
};

template<class T>
class BaseResourceManager
{
public:
    template<class K, class Func, typename... Args>
    K execute(uint32_t handle, Func &lambda, Args... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        TRY(auto resource, resource_lookup(handle));
        assert(contains(m_resources_mutexes, handle));
        std::shared_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
        lock.unlock();
        auto ret = lambda(resource->resource, args...);
        return ret;
    }

    template<class Func, typename... Args>
    hailo_status execute(uint32_t handle, Func &lambda, Args... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        TRY(auto resource, resource_lookup(handle));
        assert(contains(m_resources_mutexes, handle));
        std::shared_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
        lock.unlock();
        auto ret = lambda(resource->resource, args...);
        return ret;
    }

    uint32_t register_resource(uint32_t id, const std::shared_ptr<T> &resource)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto index = m_current_handle_index.load();
        // Create a new resource and register
        m_resources.emplace(m_current_handle_index, std::make_shared<Resource<T>>(id, std::move(resource)));
        m_resources_mutexes[m_current_handle_index]; // construct std::shared_timed_mutex
        m_current_handle_index++;
        return index;
    }

    // For cases where other resources are already registered and we want to align the indexes
    void advance_current_handle_index()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_current_handle_index++;
    }

    Expected<uint32_t> dup_handle(uint32_t handle, uint32_t id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        TRY(auto resource, resource_lookup(handle));
        assert(contains(m_resources_mutexes, handle));
        std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
        resource->ids.insert(id);

        return Expected<uint32_t>(handle);
    }

    std::shared_ptr<T> release_resource(uint32_t handle, uint32_t id)
    {
        std::shared_ptr<T> res = nullptr;
        std::unique_lock<std::mutex> lock(m_mutex);
        auto found = m_resources.find(handle);
        if (found == m_resources.end()) {
            LOGGER__INFO("Failed to release resource with handle {} and ID {}. The resource no longer exists or may have already been released",
                handle, id);
            return res;
        }

        assert(contains(m_resources_mutexes, handle));
        auto resource = m_resources[handle];
        bool release_resource = false;
        {
            std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
            resource->ids.erase(id);
            if (should_resource_be_released(resource)) {
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

    std::vector<std::shared_ptr<T>> release_by_id(uint32_t id)
    {
        std::vector<std::shared_ptr<T>> res;
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto iter = m_resources.begin(); iter != m_resources.end(); ) {
            auto handle = iter->first;
            bool release_resource = false;
            if (contains(iter->second->ids, id)) {
                assert(contains(m_resources_mutexes, handle));
                {
                    std::unique_lock<std::shared_timed_mutex> resource_lock(m_resources_mutexes[handle]);
                    iter->second->ids.erase(id);
                    if (iter->second->ids.empty()) {
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

    std::vector<uint32_t> resources_handles_by_ids(std::set<uint32_t> &ids)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        std::vector<uint32_t> resources_handles;
        for (auto &handle_resource_pair : m_resources) {
            for (auto &id : ids) {
                if (contains(handle_resource_pair.second->ids, id)) {
                    resources_handles.emplace_back(handle_resource_pair.first);
                }
            }
        }
        return resources_handles;
    }

protected:
    BaseResourceManager()
        : m_current_handle_index(0)
    {}

    virtual bool should_resource_be_released(std::shared_ptr<Resource<T>> resource) = 0;

private:
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

template<class T>
class ServiceResourceManager : public BaseResourceManager<T>
{
public:
    static ServiceResourceManager& get_instance()
    {
        static ServiceResourceManager instance;
        return instance;
    }

protected:
    virtual bool should_resource_be_released(std::shared_ptr<Resource<T>> resource) override
    {
        for (auto &id : resource->ids) {
            if (OsUtils::is_pid_alive(id)) {
                return false;
            }
        }
        return true;
    }

private:
    ServiceResourceManager() = default;
};

template<class T>
class ServerResourceManager : public BaseResourceManager<T>
{
public:
    static ServerResourceManager& get_instance()
    {
        static ServerResourceManager instance;
        return instance;
    }

protected:
    virtual bool should_resource_be_released(std::shared_ptr<Resource<T>>) override
    {
        return true;
    }

private:
    ServerResourceManager() = default;
};

}

#endif /* HAILO_SERVICE_RESOURCE_MANAGER_HPP_ */
