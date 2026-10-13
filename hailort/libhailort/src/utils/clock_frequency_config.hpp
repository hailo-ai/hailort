/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file clock_frequency_config.hpp
 * @brief Sysfs backing of the clock frequency control API.
 *
 * Every attribute file holds frequencies in MHz as decimal text: a single value for a scalar attribute, and up to
 * MAX_FREQUENCY_ENTRIES whitespace separated values for a list of operating points.
 **/

#ifndef _HAILO_CLOCK_FREQUENCY_CONFIG_HPP_
#define _HAILO_CLOCK_FREQUENCY_CONFIG_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include <string>
#include <vector>

namespace hailort
{

/**
 * One clock domain of the device, backed by a sysfs directory of frequency attribute files.
 *
 * Every accessor returns HAILO_NOT_SUPPORTED when the device does not expose the domain in sysfs.
 */
class ClockDomain final
{
public:
    // A domain exposes a handful of operating points, and this many always fit in a single attribute read
    static constexpr uint32_t MAX_FREQUENCY_ENTRIES = 32;

    /**
     * Returns HAILO_INVALID_ARGUMENT for a domain outside ::hailo_clock_domain_t.
     *
     * The domain's directory is @a sysfs_root followed by the name of the matching SCMI performance domain. Tests pass
     * a directory of their own as @a sysfs_root.
     */
    static Expected<ClockDomain> create(hailo_clock_domain_t domain, const std::string &sysfs_root = PERF_SYSFS_ROOT);

    /** Returns HAILO_FILE_OPERATION_FAILURE when the attribute holds anything other than a single nonzero frequency. */
    Expected<uint32_t> current_mhz() const;
    Expected<uint32_t> nominal_mhz() const;
    Expected<uint32_t> default_nominal_mhz() const;

    /**
     * Copies the operating points into a caller-supplied buffer, following the two-call count idiom: @a count is set
     * to the number of entries required, and a buffer too small for them returns HAILO_INSUFFICIENT_BUFFER without
     * touching @a clocks_mhz.
     *
     * A malformed list is rejected with HAILO_FILE_OPERATION_FAILURE rather than normalized, so a bad attribute stays
     * loud: the entries must be strictly ascending, and 0 MHz is not an operating point because
     * HAILO_CLOCK_FREQUENCY_DEFAULT uses it as a sentinel.
     */
    hailo_status available_nominal_mhz(uint32_t *count, uint32_t *clocks_mhz) const;

    /**
     * Selects the nominal frequency, or restores the default for HAILO_CLOCK_FREQUENCY_DEFAULT. A frequency that is
     * not one of the operating points is rejected with HAILO_INVALID_ARGUMENT, and nothing is applied.
     */
    hailo_status set_nominal_mhz(uint32_t frequency_mhz) const;

private:
    static constexpr auto PERF_SYSFS_ROOT = "/sys/devices/soc0/perf";

    static constexpr auto CURRENT_MHZ_ATTRIBUTE = "current_mhz";
    static constexpr auto NOMINAL_MHZ_ATTRIBUTE = "nominal_mhz";
    static constexpr auto DEFAULT_NOMINAL_MHZ_ATTRIBUTE = "default_nominal_mhz";
    static constexpr auto AVAILABLE_NOMINAL_MHZ_ATTRIBUTE = "available_nominal_mhz";

    explicit ClockDomain(std::string sysfs_dir);

    std::string attribute_path(const char *attribute) const;
    Expected<uint32_t> read_attribute_mhz(const char *attribute) const;
    Expected<uint32_t> read_available_nominal_mhz(uint32_t *frequencies_mhz) const;
    Expected<bool> is_available_nominal_mhz(uint32_t frequency_mhz) const;
    hailo_status write_nominal_mhz(uint32_t frequency_mhz) const;

    static constexpr const char *SYSFS_DIR_NAME_PER_DOMAIN[HAILO_CLOCK_DOMAIN_COUNT] = {
        "NN-CORE",
    };

    std::string m_sysfs_dir;
};

/**
 * Fills a caller-supplied buffer with the supported operating points, following the two-call count idiom.
 *
 * @a count is set to the number of entries required, both on success and on HAILO_INSUFFICIENT_BUFFER, which is the
 * expected result of the probe call and is not logged. A call rejected with HAILO_INVALID_ARGUMENT leaves it as it is.
 */
hailo_status fill_supported_clock_frequencies(const std::vector<uint32_t> &frequencies, uint32_t *count,
    uint32_t *clocks_mhz);

/**
 * Rejects a domain outside ::hailo_clock_domain_t with HAILO_INVALID_ARGUMENT.
 *
 * Inline so that translation units linked outside libhailort share the same range rule; libhailort exports only its
 * public API, so an out-of-line definition would not be reachable from them.
 */
inline hailo_status validate_clock_domain(hailo_clock_domain_t domain)
{
    // HAILO_CLOCK_DOMAIN_MAX_ENUM leaves the underlying type implementation-defined, so both sides are compared as
    // uint32_t: on a signed enum, a value such as UINT32_MAX would otherwise be seen as negative and pass the check
    const auto domain_value = static_cast<uint32_t>(domain);
    CHECK(static_cast<uint32_t>(HAILO_CLOCK_DOMAIN_COUNT) > domain_value, HAILO_INVALID_ARGUMENT,
        "Invalid clock domain {}", domain_value);

    return HAILO_SUCCESS;
}

} /* namespace hailort */

#endif /* _HAILO_CLOCK_FREQUENCY_CONFIG_HPP_ */
