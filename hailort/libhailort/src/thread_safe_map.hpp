/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file thread_safe_map.hpp
 * @brief Thread safe map
 **/

#ifndef HAILO_THREAD_SAFE_MAP_HPP_
#define HAILO_THREAD_SAFE_MAP_HPP_

#include <map>
#include <mutex>

namespace hailort
{

template<class K, class V>
class SafeMap {
public:
    SafeMap() : m_map(), m_mutex() {}
    virtual ~SafeMap() = default;
    SafeMap(SafeMap &&map) : m_map(std::move(map.m_map)), m_mutex() {};

    V& operator[](const K& k) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map[k];
    }

    V& operator[](K&& k) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map[k];
    }

    V& at(K& k) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.at(k);
    }

    V& at(const K& k) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.at(k);
    }

    std::size_t size() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.size();
    }

    typename std::map<K, V>::iterator find(K& k) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.find(k);
    }

    typename std::map<K, V>::iterator find(const K& k) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.find(k);
    }

    bool contains(const K &k) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.find(k) != m_map.end();
    }

    void clear() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_map.clear();
    }

    typename std::map<K, V>::iterator begin() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.begin();
    }

    typename std::map<K, V>::iterator end() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_map.end();
    }

protected:
    std::map<K, V> m_map;
    mutable std::mutex m_mutex;
};

} /* namespace hailort */

#endif // HAILO_THREAD_SAFE_MAP_HPP_
