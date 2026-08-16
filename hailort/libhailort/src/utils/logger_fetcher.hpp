/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file logger_fetcher.hpp
 * @brief LoggerFetcher is a base class for fetching logs from different source with get_max_size() and fetch_log() methods.
 **/

#ifndef _HAILO_LOGGER_FETCHER_HPP_
#define _HAILO_LOGGER_FETCHER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"
#include "common/utils.hpp"
#include "device_common/device_internal.hpp"

#include <string>

namespace hailort {

// TODO: HRT-17566
#define HAILO_RUNTIME_LOGS_MAX_SIZE_IN_BYTES (1024 * 512)   // 0.5MB
#define HAILO_SCU_LOGS_MAX_SIZE_IN_BYTES (0x1000) // 4kB
#define HAILO_NNC_LOGS_MAX_SIZE_IN_BYTES (512) // 0.5kB

#define DEFAULT_SYSLOG_DIR "/var/log"
#define DEFAULT_SYSLOG_TMP_DIR "/tmp"
#define SYSLOG_FILENAME "messages"
#define SYSLOG_ROTATED_FILENAME "messages.0"

class LoggerFetcher;
using LoggerFetcherPtr = std::shared_ptr<LoggerFetcher>;

class LoggerFetcherFactory {
public:
    static Expected<LoggerFetcherPtr> create(hailo_log_type_t type);
};

class LoggerFetcher {
public:
    virtual ~LoggerFetcher() = default;
    virtual Expected<size_t> fetch_log(MemoryView buffer, DeviceBase &device, bool should_clear = false) = 0;
    virtual size_t get_max_size() = 0;
};

class RuntimeLoggerFetcher final : public LoggerFetcher {
public:
    explicit RuntimeLoggerFetcher(const std::string &syslog_dir = DEFAULT_SYSLOG_DIR,
        const std::string &tmp_dir = DEFAULT_SYSLOG_TMP_DIR);

    virtual Expected<size_t> fetch_log(MemoryView buffer, DeviceBase &device, bool should_clear = false) override;
    virtual size_t get_max_size() override { return HAILO_RUNTIME_LOGS_MAX_SIZE_IN_BYTES; }

    Expected<size_t> drain_syslog_files(MemoryView buffer);
    Expected<size_t> read_syslog_files(MemoryView buffer);

private:
    static constexpr int MAX_ROTATION_RETRIES = 3;

    std::string syslog_path() const;
    std::string syslog_path_tmp() const;
    std::string syslog_rotated_path() const;
    std::string syslog_rotated_path_tmp() const;

    Expected<size_t> read_syslog_pair(const std::string &main_path, const std::string &rotated_path,
        MemoryView buffer, bool rotated_file_exists);

    std::string m_syslog_dir;
    std::string m_tmp_dir;
};

class ScuLoggerFetcher final : public LoggerFetcher {
public:
    virtual Expected<size_t> fetch_log(MemoryView buffer, DeviceBase &device, bool should_clear = false) override;
    virtual size_t get_max_size() override { return HAILO_SCU_LOGS_MAX_SIZE_IN_BYTES; }
};

class NncLoggerFetcher final : public LoggerFetcher {
public:
    virtual Expected<size_t> fetch_log(MemoryView buffer, DeviceBase &device, bool should_clear = false) override;
    virtual size_t get_max_size() override { return HAILO_NNC_LOGS_MAX_SIZE_IN_BYTES; }
};

} /* namespace hailort */

#endif /* _HAILO_LOGGER_FETCHER_HPP_ */