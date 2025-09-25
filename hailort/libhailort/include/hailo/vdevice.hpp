/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
     * @param[in]  params        A @a hailo_vdevice_params_t.
     * @return Upon success, returns Expected of a shared_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::shared_ptr<VDevice>> create_shared(const hailo_vdevice_params_t &params);

    /**
     * Creates a vdevice.
     * 
     * @return Upon success, returns Expected of a unique_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note calling this create method will apply default vdevice params.
     */
    static Expected<std::unique_ptr<VDevice>> create();

    /**
     * Creates a vdevice.
     * 
     * @return Upon success, returns Expected of a shared_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note calling this create method will apply default vdevice params.
     */
    static Expected<std::shared_ptr<VDevice>> create_shared();

    /**
     * Creates a vdevice from the given phyiscal device ids.
     * 
     * @param[in]  device_ids        A vector of std::string, represents the device-ids from which to create the VDevice.
     * @return Upon success, returns Expected of a unique_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note calling this create method will apply default vdevice params.
     */
    static Expected<std::unique_ptr<VDevice>> create(const std::vector<std::string> &device_ids);

    /**
     * Creates a vdevice from the given phyiscal device ids.
     * 
     * @param[in]  device_ids        A vector of std::string, represents the device-ids from which to create the VDevice.
     * @return Upon success, returns Expected of a shared_ptr to VDevice object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note calling this create method will apply default vdevice params.
     */
    static Expected<std::shared_ptr<VDevice>> create_shared(const std::vector<std::string> &device_ids);

    /**
     * Configures the vdevice from an hef.
     *
     * @param[in] hef                         A reference to an Hef object to configure the vdevice by.
     * @param[in] configure_params            A map of configured network group name and parameters.
     * @return Upon success, returns Expected of a vector of configured network groups.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={}) = 0;

    /**
     * Creates the infer model from an hef
     *
     * @param[in] hef_path                    A string of an hef file.
     * @param[in] name                        A string of the model name (optional).
     * @return Upon success, returns Expected of a shared pointer of infer model.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const std::string &hef_path,
        const std::string &name = "");

    /**
     * Creates the infer model from an hef buffer
     *
     * @param[in] hef_buffer                  A pointer to a buffer containing the hef file.
     * @param[in] name                        A string of the model name (optional).
     * @return Upon success, returns Expected of a shared pointer of infer model.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note During Hef creation, the hef_buffer's content is copied to an internal buffer.
     */
    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const MemoryView hef_buffer,
        const std::string &name = "");

    /**
     * Creates the infer model from an hef buffer
     *
     * @param[in] hef_buffer                  A pointer to a buffer containing the hef file.
     * @param[in] name                        A string of the model name (optional).
     * @return Upon success, returns Expected of a shared pointer of infer model.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(std::shared_ptr<Buffer> hef_buffer,
        const std::string &name = "");

    /**
     * Creates the infer model from an hef
     *
     * @param[in] hef                         A Hef object
     * @param[in] name                        A string of the model name (optional).
     * @return Upon success, returns Expected of a shared pointer of infer model.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(Hef hef,
        const std::string &name = "");

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
     * @return Upon success, returns Expected of a ConfigureNetworkParams.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<ConfigureNetworkParams> create_configure_params(Hef &hef, const std::string &network_group_name) const;

    /**
     * Maps the buffer pointed to by @a address for DMA transfers to/from this vdevice, in the specified
     * @a data_direction.
     * DMA mapping of buffers in advance may improve the performance of async API. This improvement will become
     * apparent when the buffer is reused multiple times across different async operations.
     *
     * For high level API (aka InferModel), buffers bound using ConfiguredInferModel::Bindings::InferStream::set_buffer
     * can be mapped.
     *
     * For low level API (aka InputStream/OutputStream), buffers passed to InputStream::write_async and
     * OutputStream::read_async can be mapped.
     *
     * @param[in] address       The address of the buffer to be mapped.
     * @param[in] size          The buffer's size in bytes.
     * @param[in] direction     The direction of the mapping. For input streams, use `HAILO_DMA_BUFFER_DIRECTION_H2D`
     *                          and for output streams, use `HAILO_DMA_BUFFER_DIRECTION_D2H`.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     *
     * @note The DMA mapping will be released upon calling dma_unmap() with @a address, @a size and @a data_direction, or
     *       when the @a VDevice object is destroyed.
     * @note The buffer pointed to by @a address cannot be released until it is unmapped (via dma_unmap() or @a VDevice
     *       destruction).
     */
    virtual hailo_status dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction) = 0;

    /**
     * Un-maps a buffer buffer pointed to by @a address for DMA transfers to/from this vdevice, in the direction
     * @a direction.
     *
     * @param[in] address       The address of the buffer to be un-mapped.
     * @param[in] size          The buffer's size in bytes.
     * @param[in] direction     The direction of the mapping.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction) = 0;

    /**
     * Maps the dmabuf represented by the file descriptor @a dmabuf_fd for DMA transfers to/from this vdevice, in the specified
     * @a data_direction.
     * DMA mapping of buffers in advance may improve the performance of async API. This improvement will become
     * apparent when the buffer is reused multiple times across different async operations.
     *
     * For high level API (aka InferModel), buffers bound using ConfiguredInferModel::Bindings::InferStream::set_buffer
     * can be mapped.
     *
     * For low level API (aka InputStream/OutputStream), buffers passed to InputStream::write_async and
     * OutputStream::read_async can be mapped.
     *
     * @param[in] dmabuf_fd     The file descriptor of the dmabuf to be mapped.
     * @param[in] size          The buffer's size in bytes.
     * @param[in] direction     The direction of the mapping. For input streams, use `HAILO_DMA_BUFFER_DIRECTION_H2D`
     *                          and for output streams, use `HAILO_DMA_BUFFER_DIRECTION_D2H`.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     *
     * @note The DMA mapping will be released upon calling dma_unmap_dmabuf() with @a dmabuf_fd, @a size and @a data_direction, or
     *       when the @a VDevice object is destroyed.
     */
    virtual hailo_status dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) = 0;

    /**
     * Un-maps a dmabuf buffer represented by the file descriptor @a dmabuf_fd for DMA transfers to/from this vdevice, in the direction
     * @a direction.
     *
     * @param[in] dmabuf_fd     The file descriptor of the dmabuf to be un-mapped.
     * @param[in] size          The buffer's size in bytes.
     * @param[in] direction     The direction of the mapping.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) = 0;

    const hailo_vdevice_params_t get_params() const {
        return m_params;
    }

    virtual hailo_status before_fork();
    virtual hailo_status after_fork_in_parent();
    virtual hailo_status after_fork_in_child();

    virtual ~VDevice() = default;
    VDevice(const VDevice &) = delete;
    VDevice &operator=(const VDevice &) = delete;
    VDevice(VDevice &&) = delete;
    VDevice &operator=(VDevice &&other) = delete;

    static bool service_over_ip_mode();
    static bool should_force_hrpc_client();

protected:
    VDevice(const hailo_vdevice_params_t &params) : m_params(params)
        {};

    hailo_vdevice_params_t m_params;
};

} /* namespace hailort */

#endif /* _HAILO_VDEVICE_HPP_ */
