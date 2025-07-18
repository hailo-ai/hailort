/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file thread_safe_map.hpp
 * @brief Thread safe map
 **/

#ifndef HAILO_THREAD_SAFE_MAP_HPP_
#define HAILO_THREAD_SAFE_MAP_HPP_

#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <shared_mutex>
#include <algorithm>

namespace hailort
{

/// Thread safe map is a wrapper to std::unordered_map std::map that allows multi-thread access to the map.
/// This class guards the map structure itself in thread safe way, and not the members.
template<typename Key, typename Value, typename MapType=std::unordered_map<Key, Value>>
class ThreadSafeMap final {
public:

    template <typename... Args>
    auto emplace(const Key& key, Args&&... args)
    {
        std::unique_lock<std::shared_timed_mutex> lock(m_mutex);
        return m_map.emplace(key, std::forward<Args>(args)...);
    }

    // Return by value (and not by reference) since after the mutex is unlocked, the reference may change.
    Value at(const Key &key) const
    {
        std::shared_lock<std::shared_timed_mutex> lock(m_mutex);
        return m_map.at(key);
    }

    // Atomically get and erase a value.
    std::pair<bool, Value> pop(const Key &key)
    {
        std::unique_lock<std::shared_timed_mutex> lock(m_mutex);
        if (!m_map.count(key)) {
            return {false, {}};
        }
        auto res = m_map[key];
        m_map.erase(key);

        return std::pair<bool, Value>(true, std::move(res));
    }

    size_t erase(const Key &key)
    {
        std::unique_lock<std::shared_timed_mutex> lock(m_mutex);
        auto iter = m_map.find(key);
        if (m_map.end() != iter) {
            return m_map.erase(key);
        }
        return 0;
    }

    void for_each(std::function<void(Value)> &&func) const
    {
        std::shared_lock<std::shared_timed_mutex> lock(m_mutex);
        for (auto key_val : m_map) {
            func(key_val.second);
        }
    }

    bool contains(const Key &key) const
    {
        std::shared_lock<std::shared_timed_mutex> lock(m_mutex);
        auto iter = m_map.find(key);
        return (m_map.end() != iter);
}

private:
    // Const operation on the map can be executed on parallel, hence we can use shared_lock, while non-const operations
    // (such as emplace) must have unique access.
    mutable std::shared_timed_mutex m_mutex;
    std::unordered_map<Key, Value> m_map;
};

} /* namespace hailort */

#endif // HAILO_THREAD_SAFE_MAP_HPP_
