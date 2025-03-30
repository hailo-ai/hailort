/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
#include <vector>

#include "common/os/windows/string_conversion.hpp"
#include "common/utils.hpp"

namespace hailort
{

Expected<std::wstring> StringConverter::ansi_to_utf16(const std::string& ansi_string)
{
    static const UINT ANSI_CODE_PAGE = CP_ACP;
    static const DWORD FAIL_ON_INVALID_CHARS = MB_ERR_INVALID_CHARS;
    static const int CALCULATE_INPUT_LENGTH = -1;
    static const LPWSTR NO_WIDE_CHAR_BUFFER = nullptr;
    static const int CALCULATE_RESULT_LENGTH = 0;
    const int required_length = MultiByteToWideChar(ANSI_CODE_PAGE, FAIL_ON_INVALID_CHARS, ansi_string.c_str(),
        CALCULATE_INPUT_LENGTH, NO_WIDE_CHAR_BUFFER, CALCULATE_RESULT_LENGTH);
    if (0 == required_length) {
        LOGGER__ERROR("Failed calculating necessary length for '{}' as unicode string. LE={}", ansi_string, GetLastError());
        return make_unexpected(HAILO_ANSI_TO_UTF16_CONVERSION_FAILED     );
    }

    std::vector<wchar_t> result_buffer(required_length, L'\0');
    const int allocated_length = MultiByteToWideChar(ANSI_CODE_PAGE, FAIL_ON_INVALID_CHARS, ansi_string.c_str(),
        CALCULATE_INPUT_LENGTH, result_buffer.data(), required_length);
    if (0 == allocated_length || allocated_length != required_length) {
        LOGGER__ERROR("Failed converting '{}' to unicode string. LE={}", ansi_string, GetLastError());
        return make_unexpected(HAILO_ANSI_TO_UTF16_CONVERSION_FAILED     );
    }

    // result_buffer includes the terminating null
    return std::wstring(result_buffer.data());
}

Expected<std::string> StringConverter::utf16_to_ansi(const std::wstring& utf16_string)
{
    static const UINT ANSI_CODE_PAGE = CP_ACP;
    static const DWORD NO_FLAGS = 0;
    static const int CALCULATE_INPUT_LENGTH = -1;
    static const LPSTR NO_UTF8_BUFFER = nullptr;
    static const int CALCULATE_RESULT_LENGTH = 0;
    static const LPCCH USE_SYSTEM_DEFAULT_CHAR = nullptr;
    static const LPBOOL NO_CUSTOM_DEFAULT_CHAR = nullptr;
    const int required_length = WideCharToMultiByte(ANSI_CODE_PAGE, NO_FLAGS, utf16_string.c_str(),
        CALCULATE_INPUT_LENGTH, NO_UTF8_BUFFER, CALCULATE_RESULT_LENGTH, USE_SYSTEM_DEFAULT_CHAR, NO_CUSTOM_DEFAULT_CHAR);
    if (0 == required_length) {
        LOGGER__ERROR("Failed calculating necessary length for utf16_string as ansi string. LE={}", GetLastError());
        return make_unexpected(HAILO_UTF16_TO_ANSI_CONVERSION_FAILED);
    }

    std::vector<char> result_buffer(required_length, '\0');
    const int allocated_length = WideCharToMultiByte(ANSI_CODE_PAGE, NO_FLAGS, utf16_string.c_str(),
        CALCULATE_INPUT_LENGTH, result_buffer.data(), required_length, USE_SYSTEM_DEFAULT_CHAR, NO_CUSTOM_DEFAULT_CHAR);
    if (0 == allocated_length || allocated_length != required_length) {
        LOGGER__ERROR("Failed converting utf16_string to ansi string. LE={}", GetLastError());
        return make_unexpected(HAILO_UTF16_TO_ANSI_CONVERSION_FAILED);
    }

    // result_buffer includes the terminating null
    return std::string(result_buffer.data());
}

} /* namespace hailort */
