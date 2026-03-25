/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file named_mutex.cpp
 * @brief Named mutex guard Linux implementation using shared memory and pthread mutex
 **/

#include "common/named_mutex.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

#include <cstdint>
#include <filesystem>

static const std::string HAILO_LOCK_FILES_DIR = "/run/lock/hailo/";
static const std::string LOCK_FILE_EXTENSION = ".lock";

namespace hailort
{

Expected<std::shared_ptr<NamedMutex>> NamedMutex::create(const std::string &path)
{
    CHECK(!path.empty(), HAILO_INVALID_ARGUMENT, "Invalid named mutex path: {}", path);
    const auto full_path = HAILO_LOCK_FILES_DIR + path + LOCK_FILE_EXTENSION;

    auto parent_path = std::filesystem::path(full_path).parent_path();
    if (!parent_path.empty() && !std::filesystem::exists(parent_path)) {
        std::error_code ec;
        std::filesystem::create_directories(parent_path, ec);
        CHECK(!ec, HAILO_FILE_OPERATION_FAILURE, "Failed to create directory {}: {}", parent_path.string(), ec.message());
    }

    TRY(auto locked_file, LockedFile::create(full_path, "w"));

    auto ptr = make_shared_nothrow<NamedMutex>(full_path, std::move(locked_file));
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

hailo_status NamedMutex::lock(std::chrono::milliseconds timeout)
{
    auto status = m_locked_file.try_lock_for(timeout);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_TIMEOUT, status);
    return HAILO_SUCCESS;
}

hailo_status NamedMutex::unlock()
{
    auto status = m_locked_file.unlock();
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

NamedMutex::~NamedMutex()
{
    // We don't unlink the locked file here because it can cause problems with processes opening the lock after it was removed
    // TODO: Remove unused lock files (HRT-20068)
}

} /* namespace hailort */
