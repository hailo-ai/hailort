/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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

#define HAILO_CLASS_PATH            "/sys/class/hailo1x"
#define HAILO_INTEGRATED_CLASS_PATH "/sys/class/hailo1x_integrated"
#define HAILO_DEVICE_ID_FILENAME    "device_id"

static inline std::string devive_type_to_class_path(DeviceType device_type)
{
    return (device_type == DeviceType::INTEGRATED) ? HAILO_INTEGRATED_CLASS_PATH : HAILO_CLASS_PATH;
}

Expected<FileDescriptor> open_device_file(const std::string &path)
{
    // Setting O_CLOEXEC to avoid leaking the driver in subprocesses that have exec'd
    // (since they load a new binary that doesn't necessarily know about the driver)
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0, HAILO_DRIVER_OPERATION_FAILED, "Failed to open device file {} with error {}", path, errno);
    return FileDescriptor(fd);
}

static Expected<std::vector<std::string>> list_devices(const std::string &class_path)
{
    std::vector<std::string> devices;
    DIR *dir_iter = opendir(class_path.c_str());
    if (!dir_iter) {
        if (ENOENT == errno) {
            // Hailo chrdev-class does not exist; meaning no devices have been detected by driver.
            return devices;
        }

        LOGGER__ERROR("Failed to open hailo class ({}): {}", class_path, errno);
        return make_unexpected(HAILO_DRIVER_INVALID_RESPONSE);
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

Expected<std::vector<DeviceInfo>> scan_devices_by_type(DeviceType device_type)
{
    const std::string class_path = devive_type_to_class_path(device_type);
    TRY(auto devices, list_devices(class_path), "Failed listing hailo devices in path {}", class_path);

    std::vector<DeviceInfo> devices_info;
    for (const auto &device_name : devices) {
        TRY(auto device_info, query_device(device_type, device_name), "Failed parsing device info for {}", device_name);
        devices_info.push_back(device_info);
    }

    return devices_info;
}

static Expected<std::string> get_line_from_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    CHECK_AS_EXPECTED(file.good(), HAILO_DRIVER_INVALID_RESPONSE, "Failed open {}", file_path);

    std::string line;
    std::getline(file, line);
    CHECK_AS_EXPECTED(file.eof(), HAILO_DRIVER_INVALID_RESPONSE, "Failed read {}", file_path);

    return line;
}

Expected<DeviceInfo> query_device(DeviceType device_type, const std::string &device_name)
{
    const std::string class_path = devive_type_to_class_path(device_type);
    const std::string device_id_path = class_path + "/" + device_name + "/" + HAILO_DEVICE_ID_FILENAME;
    TRY(auto device_id , get_line_from_file(device_id_path));

    DeviceInfo device_info = {};
    device_info.dev_path = "/dev/" + device_name;
    device_info.device_id = device_id;
    device_info.device_type = device_type;

    return device_info;
}

hailo_status convert_errno_to_hailo_status(int err, const char* ioctl_name)
{
    switch (err) {
    case ENOBUFS:
        // Expected error (when happens, can try resolve by allocating memory in different way)
        LOGGER__DEBUG("Ioctl {} failed due to insufficient amount of CMA memory", ioctl_name);
        return HAILO_RESOURCE_EXHAUSTED;
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
    case ENXIO:
        LOGGER__ERROR("Ioctl {} failed due to device not connected", ioctl_name);
        return HAILO_DEVICE_NOT_CONNECTED;
    case EAGAIN:
        LOGGER__DEBUG("Ioctl {} failed due to device temporarily unavailable", ioctl_name);
        return HAILO_DEVICE_TEMPORARILY_UNAVAILABLE;
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
