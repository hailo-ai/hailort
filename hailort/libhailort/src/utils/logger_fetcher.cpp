/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
  * @file logger_fetcher.cpp
 * @brief LoggerFetcher is a base class for fetching logs from different source with get_max_size() and fetch_log() methods.
 **/

#include "utils/logger_fetcher.hpp"
#include "common/filesystem.hpp"
#include "common/file_utils.hpp"
#include "hailo/hailort_common.hpp"

namespace hailort {

#define SYSLOG_PATH "/var/log/messages"
#define SYSLOG_PATH_TMP "/tmp/messages"
#define SYSLOG_PATH_ROTATED "/var/log/messages.0"
#define SYSLOG_PATH_ROTATED_TMP "/tmp/messages.0"
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

Expected<size_t> RuntimeLoggerFetcher::read_syslog_files(MemoryView buffer, bool rotated_file_exists)
{
    CHECK(Filesystem::does_file_exists(SYSLOG_PATH_TMP), HAILO_NOT_FOUND,
        "Main syslog file {} does not exist", SYSLOG_PATH_TMP);

    size_t rotated_syslog_size = 0;
    if (rotated_file_exists) {
        TRY(rotated_syslog_size, read_binary_file(SYSLOG_PATH_ROTATED_TMP, buffer));
    }

    MemoryView main_syslog_memview(buffer.data() + rotated_syslog_size, (buffer.size() - rotated_syslog_size));
    TRY(size_t main_syslog_size, read_binary_file(SYSLOG_PATH_TMP, main_syslog_memview));

    return rotated_syslog_size + main_syslog_size;
}

Expected<size_t> RuntimeLoggerFetcher::fetch_log(MemoryView buffer, DeviceBase &/*device*/)
{
    // Perform the following squence of operations:
    // 1. Move the current syslog file to a temporary file.
    // 2. If a rotated syslog file exists, move it to a temporary file (note: assuming no rotation will be made, to main syslog).
    // 3. Read the contents of the moved syslog files (both the main and rotated) into the provided buffer.
    // TODO: HRT-17561

    if (!Filesystem::does_file_exists(SYSLOG_PATH)) {
        return 0;
    }
    auto rename_result = std::rename(SYSLOG_PATH, SYSLOG_PATH_TMP);
    CHECK(0 == rename_result, HAILO_INTERNAL_FAILURE,
        "Failed to move syslog file from {} to {}. errno = {}", SYSLOG_PATH, SYSLOG_PATH_TMP, errno);

    bool rotated_file_exists = Filesystem::does_file_exists(SYSLOG_PATH_ROTATED);
    rename_result = std::rename(SYSLOG_PATH_ROTATED, SYSLOG_PATH_ROTATED_TMP);
    CHECK(0 == rotated_file_exists || !rename_result, HAILO_INTERNAL_FAILURE,
        "Failed to move rotated syslog file from {} to {}. errno = {}", SYSLOG_PATH_ROTATED, SYSLOG_PATH_ROTATED_TMP, errno);

    return read_syslog_files(buffer, rotated_file_exists);
}

Expected<size_t> ScuLoggerFetcher::fetch_log(MemoryView buffer, DeviceBase &/*device*/)
{
    if (!Filesystem::does_file_exists(SCU_LOG_PATH)) {
        return 0;
    }

    return read_device_file(SCU_LOG_PATH, buffer);
}

Expected<size_t> NncLoggerFetcher::fetch_log(MemoryView buffer, DeviceBase &device)
{
    return device.read_log(buffer, HAILO_CPU_ID_1);
}

} /* namespace hailort */
