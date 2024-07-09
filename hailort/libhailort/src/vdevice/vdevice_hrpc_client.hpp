/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
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

namespace hailort
{

class VDeviceHrpcClient : public VDevice
{
public:
    static Expected<std::unique_ptr<VDevice>> create(const hailo_vdevice_params_t &params);

    VDeviceHrpcClient(std::shared_ptr<hrpc::Client> client, uint32_t handle)
        : m_client(client), m_handle(handle) {}

    VDeviceHrpcClient(VDeviceHrpcClient &&) = delete;
    VDeviceHrpcClient(const VDeviceHrpcClient &) = delete;
    VDeviceHrpcClient &operator=(VDeviceHrpcClient &&) = delete;
    VDeviceHrpcClient &operator=(const VDeviceHrpcClient &) = delete;
    virtual ~VDeviceHrpcClient();

    virtual Expected<std::shared_ptr<InferModel>> create_infer_model(const std::string &hef_path,
        const std::string &network_name = "") override;
    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &hef, const NetworkGroupsParamsMap &configure_params={}) override;
    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const override;
    virtual Expected<std::vector<std::string>> get_physical_devices_ids() const override;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() const override;
    virtual hailo_status dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;

private:
    std::shared_ptr<hrpc::Client> m_client;
    uint32_t m_handle;
};

} /* namespace hailort */

#endif /* _HAILO_VDEVICE_HRPC_CLIENT_HPP_ */
