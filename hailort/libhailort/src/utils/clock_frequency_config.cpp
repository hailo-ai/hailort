/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file clock_frequency_config.cpp
 * @brief Sysfs backing of the clock frequency control API.
 **/

#include "utils/clock_frequency_config.hpp"

#include "common/file_utils.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace hailort
{

static constexpr size_t MAX_ATTRIBUTE_FILE_SIZE = 512;
// A read that fills the buffer cannot be told apart from a truncated one, so one byte is kept unused
static constexpr size_t MAX_ATTRIBUTE_CONTENT_SIZE = MAX_ATTRIBUTE_FILE_SIZE - 1;

static hailo_status check_attribute_exists(const std::string &path)
{
    std::error_code error_code;
    const auto does_file_exist = std::filesystem::exists(path, error_code);
    CHECK(!error_code, HAILO_FILE_OPERATION_FAILURE, "Failed to check whether {} exists ({})", path,
        error_code.message());

    if (!does_file_exist) {
        LOGGER__WARNING("{} does not exist, clock frequency control is not supported on this device", path);
        return HAILO_NOT_SUPPORTED;
    }

    return HAILO_SUCCESS;
}

static Expected<uint32_t> parse_frequency(const std::string &token, const std::string &path)
{
    uint32_t frequency_mhz = 0;
    const auto *const token_end = token.data() + token.size();
    const auto parse_result = std::from_chars(token.data(), token_end, frequency_mhz);
    CHECK((std::errc() == parse_result.ec) && (token_end == parse_result.ptr), HAILO_FILE_OPERATION_FAILURE,
        "{} holds \"{}\", which is not a frequency in MHz", path, token);
    CHECK(0 < frequency_mhz, HAILO_FILE_OPERATION_FAILURE, "{} holds 0 MHz, which is not an operating point", path);

    return Expected<uint32_t>(frequency_mhz);
}

static Expected<uint32_t> decode_frequencies(const std::string &content, const std::string &path,
    uint32_t *frequencies_mhz, uint32_t max_entries)
{
    uint32_t entry_count = 0;
    auto token_start = content.find_first_not_of(StringUtils::WHITESPACE_CHARS);
    while (std::string::npos != token_start) {
        const auto token_end = content.find_first_of(StringUtils::WHITESPACE_CHARS, token_start);
        CHECK(entry_count < max_entries, HAILO_FILE_OPERATION_FAILURE,
            "{} lists more frequencies than the {} a frequency attribute may hold", path, max_entries);

        TRY(const auto frequency_mhz, parse_frequency(content.substr(token_start, token_end - token_start), path));
        frequencies_mhz[entry_count] = frequency_mhz;
        entry_count++;

        token_start = content.find_first_not_of(StringUtils::WHITESPACE_CHARS, token_end);
    }

    CHECK(0 < entry_count, HAILO_FILE_OPERATION_FAILURE, "{} holds no frequency in MHz", path);

    return Expected<uint32_t>(entry_count);
}

static Expected<uint32_t> read_frequency_file(const std::string &path, uint32_t *frequencies_mhz, uint32_t max_entries)
{
    const auto file_status = check_attribute_exists(path);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, file_status);

    uint8_t content[MAX_ATTRIBUTE_FILE_SIZE];
    const auto bytes_read = read_device_file(path, MemoryView(content, sizeof(content)));
    CHECK(bytes_read, HAILO_FILE_OPERATION_FAILURE, "Failed to read {}", path);
    CHECK(bytes_read.value() <= MAX_ATTRIBUTE_CONTENT_SIZE, HAILO_FILE_OPERATION_FAILURE,
        "Content of {} is longer than the {} bytes expected of a frequency attribute", path,
        MAX_ATTRIBUTE_CONTENT_SIZE);

    return decode_frequencies(std::string(reinterpret_cast<const char *>(content), bytes_read.value()), path,
        frequencies_mhz, max_entries);
}

static hailo_status validate_frequency_list(const uint32_t *frequencies_mhz, uint32_t entry_count,
    const std::string &path)
{
    uint32_t previous_mhz = 0;
    for (uint32_t index = 0; index < entry_count; index++) {
        const auto frequency_mhz = frequencies_mhz[index];
        CHECK(previous_mhz < frequency_mhz, HAILO_FILE_OPERATION_FAILURE,
            "Expected strictly ascending frequencies in MHz, got {} MHz after {} MHz in {}", frequency_mhz,
            previous_mhz, path);

        previous_mhz = frequency_mhz;
    }

    return HAILO_SUCCESS;
}

static Expected<uint32_t> read_frequency_list(const std::string &path, uint32_t *frequencies_mhz, uint32_t max_entries)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, const auto entry_count,
        read_frequency_file(path, frequencies_mhz, max_entries));
    CHECK_SUCCESS(validate_frequency_list(frequencies_mhz, entry_count, path));

    return Expected<uint32_t>(entry_count);
}

static hailo_status prepare_frequencies_output(uint32_t required_count, uint32_t *count, uint32_t *clocks_mhz)
{
    CHECK_ARG_NOT_NULL(count);

    const auto is_buffer_large_enough = (required_count <= *count);
    if (is_buffer_large_enough) {
        CHECK_ARG_NOT_NULL(clocks_mhz);
    }

    *count = required_count;

    return is_buffer_large_enough ? HAILO_SUCCESS : HAILO_INSUFFICIENT_BUFFER;
}

hailo_status fill_supported_clock_frequencies(const std::vector<uint32_t> &frequencies, uint32_t *count,
    uint32_t *clocks_mhz)
{
    const auto required_count = static_cast<uint32_t>(frequencies.size());
    const auto output_status = prepare_frequencies_output(required_count, count, clocks_mhz);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_INSUFFICIENT_BUFFER, output_status);

    std::copy(frequencies.begin(), frequencies.end(), clocks_mhz);

    return HAILO_SUCCESS;
}

Expected<ClockDomain> ClockDomain::create(hailo_clock_domain_t domain, const std::string &sysfs_root)
{
    CHECK_SUCCESS(validate_clock_domain(domain));

    const auto *dir_name = SYSFS_DIR_NAME_PER_DOMAIN[domain];
    CHECK(nullptr != dir_name, HAILO_INTERNAL_FAILURE, "Clock domain {} has no sysfs directory",
        static_cast<uint32_t>(domain));

    return ClockDomain(sysfs_root + "/" + dir_name);
}

ClockDomain::ClockDomain(std::string sysfs_dir) :
    m_sysfs_dir(std::move(sysfs_dir))
{}

std::string ClockDomain::attribute_path(const char *attribute) const
{
    return m_sysfs_dir + "/" + attribute;
}

Expected<uint32_t> ClockDomain::read_attribute_mhz(const char *attribute) const
{
    const auto path = attribute_path(attribute);
    uint32_t frequencies_mhz[MAX_FREQUENCY_ENTRIES];
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, const auto entry_count,
        read_frequency_file(path, frequencies_mhz, MAX_FREQUENCY_ENTRIES));
    CHECK(1 == entry_count, HAILO_FILE_OPERATION_FAILURE, "Expected a single frequency in MHz in {}, got {} of them",
        path, entry_count);

    return Expected<uint32_t>(frequencies_mhz[0]);
}

Expected<uint32_t> ClockDomain::current_mhz() const
{
    return read_attribute_mhz(CURRENT_MHZ_ATTRIBUTE);
}

Expected<uint32_t> ClockDomain::nominal_mhz() const
{
    return read_attribute_mhz(NOMINAL_MHZ_ATTRIBUTE);
}

Expected<uint32_t> ClockDomain::default_nominal_mhz() const
{
    return read_attribute_mhz(DEFAULT_NOMINAL_MHZ_ATTRIBUTE);
}

Expected<uint32_t> ClockDomain::read_available_nominal_mhz(uint32_t *frequencies_mhz) const
{
    return read_frequency_list(attribute_path(AVAILABLE_NOMINAL_MHZ_ATTRIBUTE), frequencies_mhz, MAX_FREQUENCY_ENTRIES);
}

hailo_status ClockDomain::available_nominal_mhz(uint32_t *count, uint32_t *clocks_mhz) const
{
    uint32_t frequencies_mhz[MAX_FREQUENCY_ENTRIES];
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, const auto entry_count,
        read_available_nominal_mhz(frequencies_mhz));

    const auto output_status = prepare_frequencies_output(entry_count, count, clocks_mhz);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_INSUFFICIENT_BUFFER, output_status);

    memcpy(clocks_mhz, frequencies_mhz, entry_count * sizeof(uint32_t));

    return HAILO_SUCCESS;
}

Expected<bool> ClockDomain::is_available_nominal_mhz(uint32_t frequency_mhz) const
{
    uint32_t frequencies_mhz[MAX_FREQUENCY_ENTRIES];
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, const auto entry_count,
        read_available_nominal_mhz(frequencies_mhz));

    for (uint32_t index = 0; index < entry_count; index++) {
        if (frequency_mhz == frequencies_mhz[index]) {
            return Expected<bool>(true);
        }
    }

    return Expected<bool>(false);
}

hailo_status ClockDomain::write_nominal_mhz(uint32_t frequency_mhz) const
{
    const auto path = attribute_path(NOMINAL_MHZ_ATTRIBUTE);
    const auto file_status = check_attribute_exists(path);
    CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, file_status);

    const auto status = write_device_file(path, std::to_string(frequency_mhz));
    CHECK(HAILO_SUCCESS == status, HAILO_FILE_OPERATION_FAILURE, "Failed to write {} MHz to {}, status {}",
        frequency_mhz, path, status);

    return HAILO_SUCCESS;
}

hailo_status ClockDomain::set_nominal_mhz(uint32_t frequency_mhz) const
{
    auto requested_mhz = frequency_mhz;
    if (HAILO_CLOCK_FREQUENCY_DEFAULT == frequency_mhz) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, requested_mhz, default_nominal_mhz());
    }

    TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_SUPPORTED, const auto is_available, is_available_nominal_mhz(requested_mhz));
    CHECK(is_available, HAILO_INVALID_ARGUMENT,
        "{} MHz is not one of the operating points this device supports, nothing was applied", requested_mhz);

    return write_nominal_mhz(requested_mhz);
}

} /* namespace hailort */
