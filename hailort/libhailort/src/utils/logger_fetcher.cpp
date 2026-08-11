/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
  * @file logger_fetcher.cpp
 * @brief LoggerFetcher is a base class for fetching logs from different source with get_max_size() and fetch_log() methods.
 **/

#include "utils/logger_fetcher.hpp"
#include "utils/hailort_logger.hpp"
#include "common/filesystem.hpp"
#include "common/file_utils.hpp"
#include "hailo/hailort_common.hpp"

#include <sys/stat.h>

namespace hailort {

#define SCU_LOG_PATH "/dev/scu_log"

Expected<LoggerFetcherPtr> LoggerFetcherFactory::create(hailo_log_type_t type) {
    switch (type) {
    case HAILO_LOG_TYPE__RUNTIME:
        return Expected<LoggerFetcherPtr>(make_shared_nothrow<RuntimeLoggerFetcher>());
    case HAILO_LOG_TYPE__SYSTEM_CONTROL:
        return Expected<LoggerFetcherPtr>(make_shared_nothrow<ScuLoggerFetcher>());
    case HAILO_LOG_TYPE__NNC:
        return Expected<LoggerFetcherPtr>(make_shared_nothrow<NncLoggerFetcher>());
    default:
        LOGGER__ERROR("Unsupported log type: {}", HailoRTCommon::get_log_type_str(type));
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }
}

RuntimeLoggerFetcher::RuntimeLoggerFetcher(const std::string &syslog_dir, const std::string &tmp_dir) :
    m_syslog_dir(syslog_dir),
    m_tmp_dir(tmp_dir)
{}

std::string RuntimeLoggerFetcher::syslog_path() const { return m_syslog_dir + PATH_SEPARATOR + SYSLOG_FILENAME; }
std::string RuntimeLoggerFetcher::syslog_path_tmp() const { return m_tmp_dir + PATH_SEPARATOR + SYSLOG_FILENAME; }
std::string RuntimeLoggerFetcher::syslog_rotated_path() const { return m_syslog_dir + PATH_SEPARATOR + SYSLOG_ROTATED_FILENAME; }
std::string RuntimeLoggerFetcher::syslog_rotated_path_tmp() const { return m_tmp_dir + PATH_SEPARATOR + SYSLOG_ROTATED_FILENAME; }

Expected<size_t> RuntimeLoggerFetcher::read_syslog_pair(const std::string &main_path, const std::string &rotated_path,
    MemoryView buffer, bool rotated_file_exists)
{
    CHECK(Filesystem::does_file_exists(main_path), HAILO_NOT_FOUND,
        "Main syslog file {} does not exist", main_path);

    size_t rotated_syslog_size = 0;
    if (rotated_file_exists) {
        TRY(rotated_syslog_size, read_binary_file(rotated_path, buffer));
    }

    MemoryView main_syslog_memview(buffer.data() + rotated_syslog_size, (buffer.size() - rotated_syslog_size));
    TRY(size_t main_syslog_size, read_binary_file(main_path, main_syslog_memview));

    return rotated_syslog_size + main_syslog_size;
}

Expected<size_t> RuntimeLoggerFetcher::drain_syslog_files(MemoryView buffer)
{
    const auto main_path = syslog_path();
    const auto main_path_tmp = syslog_path_tmp();
    const auto rotated_path = syslog_rotated_path();
    const auto rotated_path_tmp = syslog_rotated_path_tmp();

    // Move syslog files to tmp, then read from tmp. Move (rename) is atomic on same filesystem.
    auto rename_result = std::rename(main_path.c_str(), main_path_tmp.c_str());
    CHECK(0 == rename_result, HAILO_FILE_OPERATION_FAILURE,
        "Failed to move syslog file from {} to {}. errno = {}", main_path, main_path_tmp, errno);

    bool rotated_file_exists = Filesystem::does_file_exists(rotated_path);
    if (rotated_file_exists) {
        auto rotated_rename_result = std::rename(rotated_path.c_str(), rotated_path_tmp.c_str());
        if (0 != rotated_rename_result) {
            LOGGER__WARNING("Failed to move rotated syslog file from {} to {}. errno = {}",
                rotated_path, rotated_path_tmp, errno);
            rotated_file_exists = false;
        }
    }

    auto result = read_syslog_pair(main_path_tmp, rotated_path_tmp, buffer, rotated_file_exists);

    // Clean up tmp files after reading
    std::remove(main_path_tmp.c_str());
    if (rotated_file_exists) {
        std::remove(rotated_path_tmp.c_str());
    }

    return result;
}

Expected<size_t> RuntimeLoggerFetcher::read_syslog_files(MemoryView buffer)
{
    const auto main_path = syslog_path();
    const auto rotated_path = syslog_rotated_path();

    // Reads syslog files in-place (rotated first, then main). Detects rotation via inode
    // change on the main file and retries.
    for (int retry = 0; retry < MAX_ROTATION_RETRIES; retry++) {
        // Snapshot the inode of the main syslog before reading
        struct stat stat_before = {};
        if (0 != ::stat(main_path.c_str(), &stat_before)) {
            LOGGER__DEBUG("stat() failed on {}, errno = {}", main_path, errno);
            return 0;
        }

        // Read rotated + main syslog files
        bool rotated_exists = Filesystem::does_file_exists(rotated_path);
        auto result = read_syslog_pair(main_path, rotated_path, buffer, rotated_exists);
        if (!result) {
            LOGGER__INFO("Failed to read syslog files, retrying ({}/{})",
                retry + 1, MAX_ROTATION_RETRIES);
            continue;
        }

        // Check if the main syslog file was rotated during the read (inode changed means the
        // file was renamed and a new file was created at the same path)
        struct stat stat_after = {};
        const bool is_stat_failed = (0 != ::stat(main_path.c_str(), &stat_after));
        const bool is_file_rotated = (!is_stat_failed) && (stat_before.st_ino != stat_after.st_ino);
        if (is_stat_failed || is_file_rotated) {
            LOGGER__INFO("Log rotation detected during read, retrying ({}/{})", retry + 1, MAX_ROTATION_RETRIES);
            continue;
        }

        return result.release();
    }

    LOGGER__ERROR("Log rotation detected during read, retries exhausted");
    return make_unexpected(HAILO_FILE_OPERATION_FAILURE);
}

Expected<size_t> RuntimeLoggerFetcher::fetch_log(MemoryView buffer, DeviceBase &/*device*/, bool should_clear)
{
    const auto main_path = syslog_path();
    if (!Filesystem::does_file_exists(main_path)) {
        return 0;
    }

    return should_clear ? drain_syslog_files(buffer) : read_syslog_files(buffer);
}

Expected<size_t> ScuLoggerFetcher::fetch_log(MemoryView buffer, DeviceBase &/*device*/, bool should_clear)
{
    if (should_clear) {
        LOGGER__WARNING("Clear is not supported for SCU logs, ignoring");
    }

    if (!Filesystem::does_file_exists(SCU_LOG_PATH)) {
        return 0;
    }

    return read_device_file(SCU_LOG_PATH, buffer);
}

Expected<size_t> NncLoggerFetcher::fetch_log(MemoryView buffer, DeviceBase &device, bool should_clear)
{
    return device.read_log(buffer, HAILO_CPU_ID_1, should_clear);
}

} /* namespace hailort */
