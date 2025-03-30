/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file driver_os_specific.cpp
 * @brief Implementation for QNX.
 **/

#include "vdma/driver/os/driver_os_specific.hpp"
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

extern "C" {
#include <pci/pci.h>
}

namespace hailort
{

#define HAILO_VENDOR_ID (0x1e60)
#define HAILO_PCIE_CLASS_PATH ("/dev/")
// Every device name will start with "hailo"
#define HAILO_PCIE_DEVICE_NAME_PREFIX ("hailo")

Expected<FileDescriptor> open_device_file(const std::string &path)
{
    int fd = open(path.c_str(), O_RDWR);
    CHECK(fd >= 0, HAILO_DRIVER_OPERATION_FAILED,
        "Failed to open device file {} with error {}", path, errno);
    return FileDescriptor(fd);
}

Expected<std::vector<std::string>> list_devices()
{
    DIR *dir_iter = opendir(HAILO_PCIE_CLASS_PATH);
    if (!dir_iter) {
        if (ENOENT == errno) {
            LOGGER__ERROR("Can't find hailo device.");
            return make_unexpected(HAILO_DRIVER_NOT_INSTALLED);
        }
        else {
            LOGGER__ERROR("Failed to open hailo pcie class ({}), errno {}", HAILO_PCIE_CLASS_PATH, errno);
            return make_unexpected(HAILO_DRIVER_OPERATION_FAILED);
        }
    }

    std::vector<std::string> devices;
    struct dirent *dir = nullptr;
    while ((dir = readdir(dir_iter)) != nullptr) {
        std::string device_name(dir->d_name);
        if (device_name == "." || device_name == "..") {
            continue;
        }
        // Check that it is hailo device
        if (std::string::npos == device_name.find(HAILO_PCIE_DEVICE_NAME_PREFIX)) {
            continue;
        }
        devices.push_back(device_name);
    }

    closedir(dir_iter);
    return devices;
}

Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name)
{
    HailoRTDriver::DeviceInfo dev_info = {};

    // Multiple devices not supported on QNX
    const auto index = 0;
    pci_bdf_t pci_dev = pci_device_find(index, HAILO_VENDOR_ID, PCI_DID_ANY, PCI_CCODE_ANY);
    if (PCI_BDF_NONE == pci_dev) {
        LOGGER__ERROR("Error finding relevant device");
        make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    dev_info.dev_path = std::string(HAILO_PCIE_CLASS_PATH) + device_name;
    dev_info.device_id = fmt::format("{:04X}:{:02X}:{:02X}.{}", 0, PCI_BUS(pci_dev), PCI_DEV(pci_dev), PCI_FUNC(pci_dev));

    return dev_info;
}

int run_hailo_ioctl(underlying_handle_t file, uint32_t ioctl_code, void *param) {
    int res = ioctl(file, ioctl_code, param);
    return (res < 0) ? -res : 0;
}

} /* namespace hailort */
