/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filesystem_common.cpp
 * @brief Filesystem wrapper std::filesystem common implementation
 **/

#include "common/filesystem.hpp"
#include "common/utils.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace hailort
{

Expected<std::vector<std::string>> Filesystem::get_files_in_dir_flat(const std::string &dir_path)
{
    auto std_path = fs::path(dir_path);
    if (!fs::is_directory(std_path)) {
        return make_unexpected(HAILO_OPEN_FILE_FAILURE);
    }
    std::vector<std::string> file_paths;
    for (auto const &dir_entry : fs::directory_iterator{std_path}) {
        if (dir_entry.is_regular_file()) {
            file_paths.emplace_back(dir_entry.path().string());
        }
    }
    return file_paths;
}

Expected<bool> Filesystem::is_directory(const std::string &path)
{
    return fs::is_directory(fs::path(path));
}

hailo_status Filesystem::create_directory(const std::string &dir_path)
{
    std::error_code ec;
    auto success = fs::create_directory(fs::path(dir_path), ec) || (ec.value() == 0); // (ec==0) for pre-existing
    CHECK(success, HAILO_FILE_OPERATION_FAILURE, "Failed to create directory {}", dir_path);
    return HAILO_SUCCESS;
}

hailo_status Filesystem::remove_directory(const std::string &dir_path)
{
    auto std_path = fs::path(dir_path);
    CHECK(fs::is_directory(std_path), HAILO_FILE_OPERATION_FAILURE, "{} not a directory", dir_path);
    std::error_code ec;
    auto removed = fs::remove(std_path, ec);
    CHECK(removed, HAILO_FILE_OPERATION_FAILURE, "remove({}) not removed", dir_path);
    CHECK(!ec, HAILO_FILE_OPERATION_FAILURE, "remove({}) failed {}", dir_path, ec.message());
    return HAILO_SUCCESS;
}

Expected<std::string> Filesystem::get_current_dir()
{
    std::error_code ec;
    auto path = fs::current_path(ec);
    CHECK(!ec, HAILO_FILE_OPERATION_FAILURE, "Failed to get current directory path {}", ec.message());
    return path.string();
}

bool Filesystem::does_file_exists(const std::string &path)
{
    return fs::exists(fs::path(path));
}

Expected<std::string> Filesystem::get_temp_path()
{
    std::error_code ec;
    auto path = fs::temp_directory_path(ec);
    CHECK(!ec, HAILO_FILE_OPERATION_FAILURE, "Failed to get temporary directory {}", ec.message());
    return path.string();
}

std::string Filesystem::basename(const std::string &file_name)
{
    return fs::path(file_name).filename().string();
}

} /* namespace hailort */
