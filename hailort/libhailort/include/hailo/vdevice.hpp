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


/** hailort namespace */
namespace hailort
{

class InferModel;
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
    
    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const std::string &hef_path);

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

    // TODO: Also link to async infer - ConfiguredInferModel, Bindings etc. Just like we did for
    //       InputStream::write_async and OutputStream::read_async (HRT-11039)
    /**
     * Maps the buffer pointed to by @a address for DMA transfers to/from this vdevice, in the specified
     * @a data_direction.
     * DMA mapping of buffers in advance may improve the performance of `InputStream::write_async()` or
     * `OutputStream::read_async()`. This improvement will be realized if the buffer is reused multiple times
     * across different async operations.
     * - For buffers that will be written to the vdevice via `InputStream::write_async()`, use `HAILO_H2D_STREAM`
     *   for the @a direction parameter.
     * - For buffers that will be read from the vdevice via `OutputStream::read_async()`, use `HAILO_D2H_STREAM`
     *   for the @a direction parameter.
     *
     * @param[in] address       The address of the buffer to be mapped
     * @param[in] size          The buffer's size in bytes
     * @param[in] direction     The direction of the mapping
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note The DMA mapping will be freed upon calling dma_unmap() with @a address and @a data_direction, or when the
     *       @a VDevice object is destroyed.
     * @note The buffer pointed to by @a address cannot be freed until it is unmapped (via dma_unmap() or @a VDevice
     *       destruction).
     */
    virtual hailo_status dma_map(void *address, size_t size, hailo_stream_direction_t direction);

    /**
     * Un-maps a buffer buffer pointed to by @a address for DMA transfers to/from this vdevice, in the direction
     * @a direction.
     *
     * @param[in] address       The address of the buffer to be un-mapped
     * @param[in] direction     The direction of the mapping
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status dma_unmap(void *address, hailo_stream_direction_t direction);

    virtual hailo_status before_fork();
    virtual hailo_status after_fork_in_parent();
    virtual hailo_status after_fork_in_child();

    virtual ~VDevice() = default;
    VDevice(const VDevice &) = delete;
    VDevice &operator=(const VDevice &) = delete;
    VDevice(VDevice &&) = delete;
    VDevice &operator=(VDevice &&other) = delete;

    static bool service_over_ip_mode();

protected:
    VDevice() = default;
};

} /* namespace hailort */

#endif /* _HAILO_VDEVICE_HPP_ */
