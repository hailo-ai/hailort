/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_driver_scan.cpp
 * @brief Get list and parse pcie driver info
 **/

#include "os/posix/pcie_driver_scan.hpp"
#include <dirent.h>
extern "C" {
#include <pci/pci.h>
}

namespace hailort
{

#define HAILO_VENDOR_ID (0x1e60)
#define HAILO_PCIE_CLASS_PATH ("/dev/")
// Every device name will start with "hailo"
#define HAILO_PCIE_DEVICE_NAME_PREFIX ("hailo")

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
        // Check that it is hailo device
        if (std::string::npos == device_name.find(HAILO_PCIE_DEVICE_NAME_PREFIX)) {
            continue;
        }
        devices.push_back(device_name);
    }

    closedir(dir_iter);
    return devices;
}

Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name, uint32_t index)
{
    HailoRTDriver::DeviceInfo dev_info = {};
    pci_err_t err;

    // pci_device_find finds all relevant devices - find specific using index
    pci_bdf_t pci_dev = pci_device_find(index, HAILO_VENDOR_ID, PCI_DID_ANY, PCI_CCODE_ANY);
    if (PCI_BDF_NONE == pci_dev) {
        LOGGER__ERROR("Error finding relevant device");
        make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    pci_did_t device_id;
    if (PCI_ERR_OK != (err = pci_device_read_did(pci_dev, &device_id))) {
        LOGGER__ERROR("Failed reading Device ID, error {}", err);
        make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    dev_info.dev_path = std::move(std::string(HAILO_PCIE_CLASS_PATH) + device_name);
    dev_info.vendor_id = HAILO_VENDOR_ID;
    dev_info.device_id = device_id;
    dev_info.domain = 0;
    dev_info.bus = PCI_BUS(pci_dev);
    dev_info.device = PCI_DEV(pci_dev);
    dev_info.func = PCI_FUNC(pci_dev);

    return dev_info;
}

} /* namespace hailort */
