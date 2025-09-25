/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file driver_os_specific.cpp
 * @brief Implementation for linux.
 **/

#include "vdma/driver/os/driver_os_specific.hpp"

#include "common/utils.hpp"

#include <stdarg.h>
#include <dirent.h>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

namespace hailort
{

#define HAILO_CLASS_PATH                      ("/sys/class/hailo_chardev")
#define HAILO_BOARD_LOCATION_FILENAME         ("board_location")
#define HAILO_BOARD_ACCELERATOR_TYPE_FILENAME ("accelerator_type")

Expected<FileDescriptor> open_device_file(const std::string &path)
{
    // Setting O_CLOEXEC to avoid leaking the driver in subprocesses that have exec'd
    // (since they load a new binary that doesn't necessarily know about the driver)
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0, HAILO_DRIVER_OPERATION_FAILED,
        "Failed to open device file {} with error {}", path, errno);
    return FileDescriptor(fd);
}

Expected<std::vector<std::string>> list_devices()
{
    std::vector<std::string> devices;
    DIR *dir_iter = opendir(HAILO_CLASS_PATH);
    if (!dir_iter) {
        if (ENOENT == errno) {
            // Hailo chrdev-class does not exist; meaning no devices have been detected by driver.
            return devices;
        } else {
            LOGGER__ERROR("Failed to open hailo pcie class ({}), errno {}", HAILO_CLASS_PATH, errno);
            return make_unexpected(HAILO_DRIVER_INVALID_RESPONSE);
        }
    }

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

Expected<std::vector<HailoRTDriver::DeviceInfo>> scan_devices()
{
    TRY_V(auto devices, list_devices(), "Failed listing hailo devices");

    std::vector<HailoRTDriver::DeviceInfo> devices_info;
    for (const auto &device_name : devices) {
        TRY(auto device_info, query_device_info(device_name), "Failed parsing device info for {}", device_name);
        devices_info.push_back(device_info);
    }

    return devices_info;
}

Expected<std::vector<HailoRTDriver::DeviceInfo>> scan_soc_devices()
{
    TRY(auto all_devices_info, scan_devices());

    // TODO- HRT-14078: change to be based on different classes
    std::vector<HailoRTDriver::DeviceInfo> soc_devices_info;
    for (const auto &device_info : all_devices_info) {
        if (HailoRTDriver::AcceleratorType::SOC_ACCELERATOR == device_info.accelerator_type) {
            soc_devices_info.push_back(device_info);
        }
    }

    return soc_devices_info;
}

Expected<std::vector<HailoRTDriver::DeviceInfo>> scan_nnc_devices()
{
    TRY(auto all_devices_info, scan_devices());

    // TODO- HRT-14078: change to be based on different classes
    std::vector<HailoRTDriver::DeviceInfo> nnc_devices_info;
    for (const auto &device_info : all_devices_info) {
        if (HailoRTDriver::AcceleratorType::NNC_ACCELERATOR == device_info.accelerator_type) {
            nnc_devices_info.push_back(device_info);
        }
    }

    return nnc_devices_info;
}

Expected<std::string> get_line_from_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    CHECK_AS_EXPECTED(file.good(), HAILO_DRIVER_INVALID_RESPONSE, "Failed open {}", file_path);

    std::string line;
    std::getline(file, line);
    CHECK_AS_EXPECTED(file.eof(), HAILO_DRIVER_INVALID_RESPONSE, "Failed read {}", file_path);

    return line;
}

Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name)
{
    const std::string device_id_path = std::string(HAILO_CLASS_PATH) + "/" +
        device_name + "/" + HAILO_BOARD_LOCATION_FILENAME;
    TRY(auto device_id , get_line_from_file(device_id_path));

    const std::string accelerator_type_path = std::string(HAILO_CLASS_PATH) + "/" +
        device_name + "/" + HAILO_BOARD_ACCELERATOR_TYPE_FILENAME;
    TRY(auto accelerator_type_s, get_line_from_file(accelerator_type_path));

    int accelerator_type = std::stoi(accelerator_type_s);

    HailoRTDriver::DeviceInfo device_info = {};
    device_info.dev_path = std::string("/dev/") + device_name;
    device_info.device_id = device_id;
    device_info.accelerator_type = static_cast<HailoRTDriver::AcceleratorType>(accelerator_type);

    return device_info;
}

hailo_status convert_errno_to_hailo_status(int err, const char* ioctl_name)
{
    switch (err) {
    case ENOBUFS:
        // Expected error (when happens, can try resolve by allocating memory in different way)
        LOGGER__DEBUG("Ioctl {} failed due to insufficient amount of CMA memory", ioctl_name);
        return HAILO_OUT_OF_HOST_CMA_MEMORY;
    case ENOMEM:
        LOGGER__ERROR("Ioctl {} failed due to insufficient amount of memory", ioctl_name);
        return HAILO_OUT_OF_HOST_MEMORY;
    case EFAULT:
        LOGGER__ERROR("Ioctl {} failed due to invalid address", ioctl_name);
        return HAILO_INVALID_OPERATION;
    case ECONNRESET:
        // Expected error (if the other side of the connection is closed)
        LOGGER__DEBUG("Ioctl {} failed due to stream abort", ioctl_name);
        return HAILO_STREAM_ABORT;
    case ENOTTY:
        LOGGER__ERROR("Ioctl {} failed due to inappropriate ioctl for device (can happen due to version mismatch or unsupported feature)", ioctl_name);
        return HAILO_DRIVER_INVALID_IOCTL;
    case ETIMEDOUT:
        LOGGER__ERROR("Ioctl {} failed due to timeout", ioctl_name);
        return HAILO_DRIVER_TIMEOUT;
    case EINTR:
        LOGGER__ERROR("Ioctl {} failed due to interrupted system call", ioctl_name);
        return HAILO_DRIVER_INTERRUPTED;
    case ECONNREFUSED:
        LOGGER__ERROR("Ioctl {} failed due to connection refused", ioctl_name);
        return HAILO_CONNECTION_REFUSED;
    case ECANCELED:
        // Expected error (stopping wait, i.e notification wait)
        LOGGER__DEBUG("Ioctl {} failed due to operation aborted", ioctl_name);
        return HAILO_DRIVER_WAIT_CANCELED;
    default:
        LOGGER__ERROR("Ioctl {} failed with {}. Read dmesg log for more info", ioctl_name, err);
        return HAILO_DRIVER_OPERATION_FAILED;
    }
}

int run_hailo_ioctl(underlying_handle_t file, uint32_t ioctl_code, void *param) {
    int res = ioctl(file, ioctl_code, param);
    return (res < 0) ? errno : 0;
}

} /* namespace hailort */
