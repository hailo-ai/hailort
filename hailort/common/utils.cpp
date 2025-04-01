/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file utils.cpp
 * @brief Utilities for libhailort
 **/

#include "common/utils.hpp"

#include <sstream>
#include <stdlib.h>
#include <errno.h>

namespace hailort
{

// TODO: make it templated
Expected<uint32_t> StringUtils::to_uint32(const std::string &str, int base)
{
    errno = 0;
    char *end_pointer = nullptr;

    auto value = strtoul(str.c_str(), &end_pointer, base);
    static_assert(sizeof(value) >= sizeof(uint32_t), "Size of value must be equal or greater than size of uint32_t");
    CHECK_AS_EXPECTED(errno == 0, HAILO_INVALID_ARGUMENT, "Failed to convert string {} to uint32_t. strtoul failed with errno {}", str, errno);
    CHECK_AS_EXPECTED(((*end_pointer == '\0') || (*end_pointer == '\n')  || (*end_pointer == ' ') || (*end_pointer == '\r')),
        HAILO_INVALID_ARGUMENT, "Failed to convert string {} to uint32_t. strtoul failed with errno {}", str, errno);
    if ((value == 0) && (end_pointer == str)) {
        LOGGER__ERROR("Failed to convert string {} to uint32_t.", str);
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    CHECK_AS_EXPECTED(((value >= std::numeric_limits<uint32_t>::min()) && (value <= std::numeric_limits<uint32_t>::max())), 
        HAILO_INVALID_ARGUMENT, "Failed to convert string {} to uint32_t.", str);

    return static_cast<uint32_t>(value);
}

// TODO: make it templated
Expected<int32_t> StringUtils::to_int32(const std::string &str, int base)
{
    errno = 0;
    char *end_pointer = nullptr;

    auto value = strtol(str.c_str(), &end_pointer, base);
    static_assert(sizeof(value) >= sizeof(int32_t), "Size of value must be equal or greater than size of int32_t");
    CHECK_AS_EXPECTED(errno == 0, HAILO_INVALID_ARGUMENT, "Failed to convert string {} to int32_t. strtol failed with errno {}", str, errno);
    CHECK_AS_EXPECTED(((*end_pointer == '\0') || (*end_pointer == '\n')  || (*end_pointer == ' ') || (*end_pointer == '\r')),
        HAILO_INVALID_ARGUMENT, "Failed to convert string {} to int32_t. strtoul failed with errno {}", str, errno);
    if ((value == 0) && (end_pointer == str)) {
        LOGGER__ERROR("Failed to convert string {} to int32_t.", str);
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    CHECK_AS_EXPECTED(((value >= std::numeric_limits<int32_t>::min()) && (value <= std::numeric_limits<int32_t>::max())), 
        HAILO_INVALID_ARGUMENT, "Failed to convert string {} to int32.", str);

    return static_cast<int32_t>(value);
}

Expected<uint8_t> StringUtils::to_uint8(const std::string &str, int base)
{
    TRY(const auto number, to_uint32(str, base));

    CHECK_AS_EXPECTED(((number >= std::numeric_limits<uint8_t>::min()) && (number <= std::numeric_limits<uint8_t>::max())), 
        HAILO_INVALID_ARGUMENT, "Failed to convert string {} to uint8_t.", str);

    return static_cast<uint8_t>(number);
}

std::string StringUtils::to_hex_string(const uint8_t *array, size_t size, bool uppercase, const std::string &delimiter)
{
    std::stringstream stream;
    for (size_t i = 0; i < size; i++) {
        const auto hex_byte = uppercase ? fmt::format("{:02X}", array[i]) : fmt::format("{:02x}", array[i]);
        stream << hex_byte;
        if (i != (size - 1)) {
            stream << delimiter;
        }
    }
    return stream.str();
}

} /* namespace hailort */
