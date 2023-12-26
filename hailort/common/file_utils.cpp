/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file file_utils.cpp
 * @brief Utilities for file operations
 **/

#include "common/file_utils.hpp"
#include "common/utils.hpp"
#include <fstream>

namespace hailort
{

Expected<size_t> get_istream_size(std::ifstream &s)
{
    auto beg_pos = s.tellg();
    CHECK_AS_EXPECTED(-1 != beg_pos, HAILO_FILE_OPERATION_FAILURE, "ifstream::tellg() failed");

    s.seekg(0, s.end);
    CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    auto size = s.tellg();
    CHECK_AS_EXPECTED(-1 != size, HAILO_FILE_OPERATION_FAILURE, "ifstream::tellg() failed");

    s.seekg(beg_pos, s.beg);
    CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    auto total_size = static_cast<uint64_t>(size - beg_pos);
    CHECK_AS_EXPECTED(total_size <= std::numeric_limits<size_t>::max(), HAILO_FILE_OPERATION_FAILURE,
        "File size {} is too big", total_size);
    return Expected<size_t>(static_cast<size_t>(total_size));
}

Expected<Buffer> read_binary_file(const std::string &file_path, const BufferStorageParams &output_buffer_params)
{
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    CHECK_AS_EXPECTED(file.good(), HAILO_OPEN_FILE_FAILURE, "Error opening file {}", file_path);

    auto file_size = get_istream_size(file);
    CHECK_EXPECTED(file_size, "Failed to get file size");

    auto buffer = Buffer::create(file_size.value(), output_buffer_params);
    CHECK_EXPECTED(buffer, "Failed to allocate file buffer ({} bytes}", file_size.value());

    // Read the data
    file.read(reinterpret_cast<char*>(buffer->data()), buffer->size());
    CHECK_AS_EXPECTED(file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading file {}", file_path);
    return buffer.release();
}

} /* namespace hailort */
