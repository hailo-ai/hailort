/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_hrpc_client.hpp
 * @brief VDevice HRPC client, represents the user's handle to the VDevice object (held in the hailort server)
 **/

#ifndef _HAILO_VDEVICE_HRPC_CLIENT_HPP_
#define _HAILO_VDEVICE_HRPC_CLIENT_HPP_

#include "hailo/hailort.h"
#include "hrpc/client.hpp"
#include "vdevice/vdevice_internal.hpp"
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"
#include "device/device_hrpc_client.hpp"

namespace hailort
{
class VDeviceHrpcClient : public VDevice
{
public:
    static Expected<std::unique_ptr<VDevice>> create(const hailo_vdevice_params_t &params);
    static Expected<std::vector<std::string>> get_device_ids(const hailo_vdevice_params_t &params);

    VDeviceHrpcClient(const hailo_vdevice_params_t &params, std::shared_ptr<Client> client, uint32_t handle,
        std::shared_ptr<ClientCallbackDispatcherManager> callback_dispatcher_manager,
        std::unique_ptr<Device> &&device, std::string device_id)
        : VDevice(params), m_client(client), m_handle(handle), m_callback_dispatcher_manager(callback_dispatcher_manager),
            m_device(std::move(device)), m_device_id(device_id) {}

    VDeviceHrpcClient(VDeviceHrpcClient &&) = delete;
    VDeviceHrpcClient(const VDeviceHrpcClient &) = delete;
    VDeviceHrpcClient &operator=(VDeviceHrpcClient &&) = delete;
    VDeviceHrpcClient &operator=(const VDeviceHrpcClient &) = delete;
    virtual ~VDeviceHrpcClient();

    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const std::string &hef_path,
        const std::string &name = "") override;
    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const MemoryView hef_buffer,
        const std::string &name = "") override;
    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &hef, const NetworkGroupsParamsMap &configure_params={}) override;
    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const override;
    virtual Expected<std::vector<std::string>> get_physical_devices_ids() const override;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() const override;
    virtual hailo_status dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;

private:
    static Expected<std::tuple<std::shared_ptr<Client>, rpc_object_handle_t>>
        create_available_vdevice(const std::vector<std::string> &device_ids, const hailo_vdevice_params_t &params);

    std::shared_ptr<Client> m_client;
    uint32_t m_handle;
    std::shared_ptr<ClientCallbackDispatcherManager> m_callback_dispatcher_manager;
    std::unique_ptr<Device> m_device;
    std::string m_device_id;
};

// Empty implementation for forced-socket in the client side
class VDeviceSocketBasedClient : public VDevice
{
public:
    static Expected<std::unique_ptr<VDevice>> create(const hailo_vdevice_params_t &params);

    VDeviceSocketBasedClient(const hailo_vdevice_params_t &params)
        : VDevice(params) {}

    VDeviceSocketBasedClient(VDeviceSocketBasedClient &&) = delete;
    VDeviceSocketBasedClient(const VDeviceSocketBasedClient &) = delete;
    VDeviceSocketBasedClient &operator=(VDeviceSocketBasedClient &&) = delete;
    VDeviceSocketBasedClient &operator=(const VDeviceSocketBasedClient &) = delete;
    virtual ~VDeviceSocketBasedClient() = default;

    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const std::string&, const std::string&) override
    {
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }
    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const MemoryView, const std::string &) override
    {
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }
    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef&, const NetworkGroupsParamsMap&) override
    {
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }
    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const override
    {
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }
    virtual Expected<std::vector<std::string>> get_physical_devices_ids() const override
    {
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() const override
    {
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }
    virtual hailo_status dma_map(void*, size_t, hailo_dma_buffer_direction_t) override
    {
        return HAILO_NOT_IMPLEMENTED;
    }
    virtual hailo_status dma_unmap(void*, size_t, hailo_dma_buffer_direction_t) override
    {
        return HAILO_NOT_IMPLEMENTED;
    }
    virtual hailo_status dma_map_dmabuf(int, size_t, hailo_dma_buffer_direction_t) override
    {
        return HAILO_NOT_IMPLEMENTED;
    }
    virtual hailo_status dma_unmap_dmabuf(int, size_t, hailo_dma_buffer_direction_t) override
    {
        return HAILO_NOT_IMPLEMENTED;
    }
};


} /* namespace hailort */

#endif /* _HAILO_VDEVICE_HRPC_CLIENT_HPP_ */
