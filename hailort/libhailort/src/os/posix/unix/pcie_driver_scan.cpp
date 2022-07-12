/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_driver_scan.cpp
 * @brief Parse pcie driver sysfs
 **/

#include "os/posix/pcie_driver_scan.hpp"
#include <stdarg.h>
#include <dirent.h>

namespace hailort
{

#define HAILO_PCIE_CLASS_PATH ("/sys/class/hailo_chardev")
#define HAILO_BOARD_LOCATION_FILENAME ("board_location")
#define HAILO_DEVICE_ID_FILENAME ("device_id")


Expected<std::vector<std::string>> list_pcie_devices()
{
    DIR *dir_iter = opendir(HAILO_PCIE_CLASS_PATH);
    if (!dir_iter) {
        if (ENOENT == errno) {
            LOGGER__ERROR("Can't find hailo pcie class, this may happen if the driver is not installed (this may happen"
            " if the kernel was updated), or if there is no connected Hailo board");
            return make_unexpected(HAILO_PCIE_DRIVER_NOT_INSTALLED);
        }
        else {
            LOGGER__ERROR("Failed to open hailo pcie class ({}), errno {}", HAILO_PCIE_CLASS_PATH, errno);
            return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
        }
    }

    std::vector<std::string> devices;
    struct dirent *dir = nullptr;
    while ((dir = readdir(dir_iter)) != nullptr) {
        std::string device_name(dir->d_name);
        if (device_name == "." || device_name == "..") {
            continue;
        }
        devices.push_back(device_name);
    }

    closedir(dir_iter);
    return devices;
}

/**
 * Parses hailo driver sysfs entry using scanf format string
 * @param device_name - name of the specific device (inside hailo class sysfs directory)
 * @param sysfs_file_name - file name inside the device sysfs directory
 * @param expected_count - expected amount of scanf variable
 * @param fscanf_format - scanf format of the file
 * @param ... - external arguments, filled by the scanf
 */
__attribute__((__format__ (__scanf__, 4, 5)))
static hailo_status parse_device_sysfs_file(const std::string &device_name, const std::string &sysfs_file_name, uint32_t expected_count,
    const char *fscanf_format, ...)
{
    std::string sysfs_file_path = std::string(HAILO_PCIE_CLASS_PATH) + "/" +
        device_name + "/" + sysfs_file_name;
    FILE *file_obj = fopen(sysfs_file_path.c_str(), "r");
    if (!file_obj) {
        LOGGER__ERROR("Failed opening sysfs file {}, errno {}", sysfs_file_path, errno);
        return HAILO_FILE_OPERATION_FAILURE;
    }

    va_list args;
    va_start(args, fscanf_format);
    auto items_count = vfscanf(file_obj, fscanf_format, args);
    va_end(args);
    fclose(file_obj);

    if (static_cast<uint32_t>(items_count) != expected_count) {
        LOGGER__ERROR("Invalid sysfs file format {}", sysfs_file_path);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name)
{
    HailoRTDriver::DeviceInfo device_info = {};
    device_info.dev_path = std::string("/dev/") + device_name;

    auto status = parse_device_sysfs_file(device_name, HAILO_BOARD_LOCATION_FILENAME, 4, "%04x:%02x:%02x.%d",
        &device_info.domain, &device_info.bus, &device_info.device, &device_info.func);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed reading {} file", HAILO_BOARD_LOCATION_FILENAME);

    status = parse_device_sysfs_file(device_name, HAILO_DEVICE_ID_FILENAME, 2, "%x:%x",
        &device_info.vendor_id, &device_info.device_id);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed reading {} file", HAILO_DEVICE_ID_FILENAME);

    return device_info;
}

} /* namespace hailort */
