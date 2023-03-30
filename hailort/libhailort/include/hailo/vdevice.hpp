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
     * Creates a vdevice from the given phyiscal device ids.
     * 
     * @return Upon success, returns Expected of a unique_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note calling this create method will apply default vdevice params.
     */
    static Expected<std::unique_ptr<VDevice>> create(const std::vector<std::string> &device_ids);

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
    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const = 0;

    /**
     * Gets the physical device IDs.
     * 
     * @return Upon success, returns Expected of a vector of std::string device ids objects.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<std::string>> get_physical_devices_ids() const = 0;

    /**
     * Gets the stream's default interface.
     *
     * @return Upon success, returns Expected of ::hailo_stream_interface_t.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() const = 0;

    /**
     * Create the default configure params from an hef.
     *
     * @param[in] hef                A reference to an Hef object to create configure params by
     * @return Upon success, returns Expected of a NetworkGroupsParamsMap (map of string and ConfiguredNetworkParams).
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<NetworkGroupsParamsMap> create_configure_params(Hef &hef) const;

    /**
     * Create the default configure params from an hef.
     *
     * @param[in] hef                         A reference to an Hef object to create configure params by
     * @param[in] network_group_name          Name of network_group to make configure params for.
     * @return Upon success, returns Expected of a NetworkGroupsParamsMap (map of string and ConfiguredNetworkParams).
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<ConfigureNetworkParams> create_configure_params(Hef &hef, const std::string &network_group_name) const;

    virtual hailo_status before_fork() { return HAILO_SUCCESS; }
    virtual hailo_status after_fork_in_parent() { return HAILO_SUCCESS; }
    virtual hailo_status after_fork_in_child() { return HAILO_SUCCESS; }

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
