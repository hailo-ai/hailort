/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file profiler_utils.hpp
 * @brief Utils for profiling mechanism for HailoRT + FW events
 **/

#ifndef _HAILO_PROFILER_UTILS_HPP_
#define _HAILO_PROFILER_UTILS_HPP_

#include "utils/hailort_logger.hpp"

#if defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

namespace hailort
{
#define PCIE_GEN1_SPEED "2.5GT/s"
#define PCIE_GEN2_SPEED "5GT/s"
#define PCIE_GEN3_SPEED "8GT/s"
#define PCIE_GEN4_SPEED "16GT/s"
#define PCIE_GEN5_SPEED "32GT/s"
#define PCIE_GEN6_SPEED "64GT/s"

struct ProfilerTime {
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t min;
    int64_t time_since_epoch;
};

struct pci_info {
    std::string gen;
    std::string lanes;

    pci_info() : gen("N/A"), lanes("N/A") {}
};

#if defined(__linux__)
std::string os_name()
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        LOGGER__ERROR("Failed to fetch os name.");
        return "";
    }
    return uts.sysname;
}

std::string os_ver()
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        LOGGER__ERROR("Failed to fetch os ver.");
        return "";
    }
    return uts.version;
}

std::string cpu_arch()
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        LOGGER__ERROR("Failed to fetch cpu architecture.");
        return "";
    }
    return uts.machine;
}

std::uint64_t system_ram_size()
{
    struct sysinfo sys_info;

    if (sysinfo(&sys_info) != 0) {
        LOGGER__ERROR("Failed to fetch system ram size.");
        return 1;
    }

    return sys_info.totalram;
}

std::string exec(const char *cmd) {
    const int buffer_size = 128;
    std::array<char, buffer_size> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);

    if (!pipe) {
        LOGGER__WARNING("Couldn't execute {}, popen() failed!", cmd);
        return "";
    }

    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), buffer_size, pipe.get()) != nullptr) {
            result += buffer.data();
        }
    }

    return result;
}

pci_info parse_lspci_output(const std::string &output) {
    std::istringstream lspci_stream(output);
    pci_info pcie_info = {};
    std::string line;
    bool in_hailo_section = false;
    int hailo_device_count = 0;

    while (std::getline(lspci_stream, line)) {
        // Sample output line: "LnkCap:	Port #0, Speed 8GT/s, Width x8, ASPM L0s L1, Exit Latency L0s <256ns, L1 <4us"
        if (line.find("Co-processor: Hailo") != std::string::npos) {
            in_hailo_section = true;
            hailo_device_count++;
            // TODO: HRT-8834/8835 Support multiple Hailo devices connected to the same host
            if (1 < hailo_device_count) {
                pcie_info.gen = "N/A";
                pcie_info.lanes = "N/A";
                return pcie_info;
            }
        }
        if (!in_hailo_section) {
            continue;
        }
        if (line.find("LnkCap") != std::string::npos) {
            std::istringstream line_stream(line);
            std::string token;
            while (line_stream >> token) {
                if ("Speed" == token) {
                    line_stream >> token;
                    if (!token.empty() && token.back() == ',') {
                        token.pop_back();
                    }
                    if (PCIE_GEN1_SPEED == token) { pcie_info.gen = "1"; }
                    else if (PCIE_GEN2_SPEED == token) { pcie_info.gen = "2"; }
                    else if (PCIE_GEN3_SPEED == token) { pcie_info.gen = "3"; }
                    else if (PCIE_GEN4_SPEED == token) { pcie_info.gen = "4"; }
                    else if (PCIE_GEN5_SPEED == token) { pcie_info.gen = "5"; }
                    else if (PCIE_GEN6_SPEED == token) { pcie_info.gen = "6"; }
                }
                if ("Width" == token) {
                    line_stream >> token;
                    pcie_info.lanes = token.substr(1);
                }
            }
        }
    }
    return pcie_info;
}

pci_info get_pcie_info() {
    std::string lspci_output = exec("lspci -vvv");
    return parse_lspci_output(lspci_output);
}
#endif

ProfilerTime get_curr_time()
{
    ProfilerTime curr_time = {};
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct std::tm t_time = *std::localtime(&time);

    curr_time.day = t_time.tm_mday;
    // Months in std::tm are 0-based
    curr_time.month = t_time.tm_mon + 1;
    // Years since 1900
    curr_time.year = t_time.tm_year + 1900;
    curr_time.hour = t_time.tm_hour;
    curr_time.min = t_time.tm_min;
    curr_time.time_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>
        (std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    return curr_time;
}

std::string get_libhailort_version_representation()
{
    std::string result = "";
    hailo_version_t libhailort_version = {};
    auto status = hailo_get_library_version(&libhailort_version);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to fetch libhailort version");
        return result;
    }

    result = result + std::to_string(libhailort_version.major) + "." + std::to_string(libhailort_version.minor) + "." +
        std::to_string(libhailort_version.revision);
    return result;
}

}

#endif // _HAILO_PROFILER_UTILS_HPP_
