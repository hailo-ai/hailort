/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file string_conversion.hpp
 * @brief Safe string encoding conversions.
 **/

#ifndef _OS_STRING_CONVERSION_HPP_
#define _OS_STRING_CONVERSION_HPP_

#include <string>

#include <hailo/platform.h>
#include <hailo/hailort.h>
#include "hailo/expected.hpp"

namespace hailort
{

class StringConverter final
{
public:
    StringConverter() = delete;

    static Expected<std::wstring> ansi_to_utf16(const std::string& ansi_string);
    static Expected<std::string> utf16_to_ansi(const std::wstring& utf16_string);
};


} /* namespace hailort */

#endif /* _OS_STRING_CONVERSION_HPP_ */
