/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file file_utils.hpp
 * @brief Utilities for file operations
 **/

#ifndef _HAILO_FILE_UTILS_HPP_
#define _HAILO_FILE_UTILS_HPP_

#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

namespace hailort
{

/**
 * Returns the amount of data left in the given file.
 */
Expected<size_t> get_istream_size(std::ifstream &s);

/**
 * Reads full file content into a `Buffer`
 */
Expected<Buffer> read_binary_file(const std::string &file_path,
    const BufferStorageParams &output_buffer_params = {});

} /* namespace hailort */

#endif /* _HAILO_FILE_UTILS_HPP_ */
