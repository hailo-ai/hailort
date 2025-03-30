/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file named_mutex_guard.hpp
 * @brief Named mutex guard
 **/

#ifndef _HAILO_NAMED_MUTEX_GUARD_HPP_
#define _HAILO_NAMED_MUTEX_GUARD_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include <string>
#include <memory>

namespace hailort
{

class NamedMutexGuard
{
public:
    static Expected<std::unique_ptr<NamedMutexGuard>> create(const std::string &named_mutex);

    NamedMutexGuard(NamedMutexGuard &&) = delete;
    NamedMutexGuard(const NamedMutexGuard &) = delete;
    NamedMutexGuard &operator=(NamedMutexGuard &&) = delete;
    NamedMutexGuard &operator=(const NamedMutexGuard &) = delete;
    virtual ~NamedMutexGuard();

    NamedMutexGuard(HANDLE mutex_handle);
private:
    HANDLE m_mutex_handle;
};

} /* namespace hailort */

#endif /* _HAILO_NAMED_MUTEX_GUARD_HPP_ */
