/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file common.cpp
 * @brief Common functions.
 **/
#include "common.hpp"
#include "common/utils.hpp"

#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>

std::string CliCommon::duration_to_string(std::chrono::seconds secs)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    bool neg = (secs < 0s);
    if (neg) {
        secs = -secs;
    }

    auto h = duration_cast<hours>(secs);
    secs -= h;
    auto m = duration_cast<minutes>(secs);
    secs -= m;

    std::stringstream result;
    if (neg) {
        result << '-';
    }

    if (h < 10h) {
        result << '0';
    }
    result << (h/1h) << ':';

    if (m < 10min) {
        result << '0';
    }
    result << m/1min << ':';

    if (secs < 10s) {
        result << '0';
    }
    result << secs/1s;

    return result.str();
}


Expected<std::string> CliCommon::current_time_to_string()
{
    const auto curr_time = std::time(nullptr);
    CHECK_AS_EXPECTED(static_cast<std::time_t>(-1) != curr_time, HAILO_INTERNAL_FAILURE, "std::time failed");
    const auto *local_time = std::localtime(&curr_time);
    CHECK_AS_EXPECTED(nullptr != local_time, HAILO_INTERNAL_FAILURE, "std::localtime failed");

    std::stringstream result;
    // Standard date and time string (see: https://en.cppreference.com/w/cpp/io/manip/put_time)
    result << std::put_time(local_time, "%c");
    return result.str();
}

void CliCommon::reset_cursor(size_t lines_count)
{
    for (size_t i = 0; i < lines_count; i++) {
        std::cout << FORMAT_CURSOR_UP_LINE;
    }
}