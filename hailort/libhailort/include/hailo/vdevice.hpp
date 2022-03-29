/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice.hpp
 * @brief Hailo virtual device representation of multiple physical pcie devices
 **/

#ifndef _HAILO_VDEVICE_HPP_
#define _HAILO_VDEVICE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hef.hpp"
#include "hailo/network_group.hpp"
#include "hailo/device.hpp"

namespace hailort
{

/*! Represents a bundle of physical devices. */
class HAILORTAPI VDevice
{
public:

    /**
     * Creates a vdevice.
     * 
     * @param[in]  params        A @a hailo_vdevice_params_t.
     * @return Upon success, returns Expected of a unique_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<VDevice>> create(const hailo_vdevice_params_t &params);

    /**
     * Creates a vdevice.
     * 
     * @return Upon success, returns Expected of a unique_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note calling this create method will apply default vdevice params.
     */
    static Expected<std::unique_ptr<VDevice>> create();

    /**
     * Configure the vdevice from an hef.
     *
     * @param[in] hef                         A reference to an Hef object to configure the vdevice by.
     * @param[in] configure_params            A map of configured network group name and parameters.
     * @return Upon success, returns Expected of a vector of configured network groups.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={}) = 0;

    /**
     * Gets the underlying physical devices.
     * 
     * @return Upon success, returns Expected of a vector of device objects.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note The returned physical devices are held in the scope of @a vdevice.
     */
    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() = 0;

    /**
     * Gets the devices informations.
     * 
     * @return Upon success, returns Expected of a vector of ::hailo_pcie_device_info_t objects.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<hailo_pcie_device_info_t>> get_physical_devices_infos() = 0;

    virtual ~VDevice() = default;
    VDevice(const VDevice &) = delete;
    VDevice &operator=(const VDevice &) = delete;
    VDevice(VDevice &&) = delete;
    VDevice &operator=(VDevice &&other) = delete;

protected:
    VDevice() = default;
};

} /* namespace hailort */

#endif /* _HAILO_VDEVICE_HPP_ */
