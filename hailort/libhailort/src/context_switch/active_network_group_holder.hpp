/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file active_network_group_holder.hpp
 * @brief place_holder stored in ConfigManager indicating which ConfiguredNetworkGroup is currently active
 *
 **/

#ifndef _HAILO_CONTEXT_SWITCH_ACTIVE_NETWORK_GROUP_HOLDER_HPP_
#define _HAILO_CONTEXT_SWITCH_ACTIVE_NETWORK_GROUP_HOLDER_HPP_

// TODO: cant we just have ActiveNetworkGroup ref under device?

#include "hailo/hailort.h"
#include "common/utils.hpp"

namespace hailort
{

template <typename T> 
class ActiveNetworkGroupHolder final
{
  public:
    ActiveNetworkGroupHolder() : m_net_group(nullptr) {}

    ExpectedRef<T> get()
    {
        CHECK_NOT_NULL_AS_EXPECTED(m_net_group, HAILO_INVALID_OPERATION);
        return std::ref(*m_net_group);
    }
    void set(T &net_group)
    {
        assert(!is_any_active());
        m_net_group = &net_group;
    }

    bool is_any_active() { return nullptr != m_net_group; }

    void clear() { m_net_group = nullptr; }

    ActiveNetworkGroupHolder(ActiveNetworkGroupHolder&) = delete;
    ActiveNetworkGroupHolder& operator=(ActiveNetworkGroupHolder&) = delete;
    ActiveNetworkGroupHolder& operator=(ActiveNetworkGroupHolder&&) = delete;
    ActiveNetworkGroupHolder(ActiveNetworkGroupHolder&&) = default;
  private:
    T *m_net_group;
};

} /* namespace hailort */

#endif //_HAILO_CONTEXT_SWITCH_ACTIVE_NETWORK_GROUP_HOLDER_HPP_