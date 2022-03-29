/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file string_utils.hpp
 * @brief Defines utilities methods for string.
 **/

#ifndef _HAILO_STRING_UTILS_HPP_
#define _HAILO_STRING_UTILS_HPP_

#include "hailo/expected.hpp"
#include <string>

namespace hailort
{

class StringUtils {
public:
    static Expected<int32_t> to_int32(const std::string &str, int base);
    static Expected<uint8_t> to_uint8(const std::string &str, int base);
    static Expected<uint32_t> to_uint32(const std::string &str, int base);
};

} /* namespace hailort */

#endif /* _HAILO_STRING_UTILS_HPP_ */
