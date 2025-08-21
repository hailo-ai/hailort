/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_internal.hpp
 * @brief Class declaration for VDeviceBase that implements the basic VDevice "interface".
 *        Hence, the hiearchy is as follows:
 *
 * VDevice                  (External "interface")
 * |-- VDeviceHandle                   (VDevice handle for a possibly shared VDeviceBase when hailort is running as single process)
 * |-- VDeviceClient                   (VDevice client for a possibly shared VDeviceBase when hailort is running as a service)
 * |-- VDeviceHrpcClient               (RPC handle communicating with the server)
 * |-- VDeviceSocketBasedClient        (Empty implementation for forced-socket in the client side)
 * |-- VDeviceBase          (Actual implementations)
 *     |
 *     |-- std::vector<VdmaDevice>
 **/

#ifndef _HAILO_VDEVICE_INTERNAL_HPP_
#define _HAILO_VDEVICE_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"

#include "common/async_thread.hpp"
#include "common/internal_env_vars.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/vdma_config_manager.hpp"
#include "vdevice/vdevice_core_op.hpp"
#include "vdevice/scheduler/scheduler.hpp"


namespace hailort
{

class VDeviceBase : public VDevice
{
public:
    static Expected<std::unique_ptr<VDeviceBase>> create(const hailo_vdevice_params_t &params);
    VDeviceBase(VDeviceBase &&) = delete;
    VDeviceBase(const VDeviceBase &) = delete;
    VDeviceBase &operator=(VDeviceBase &&) = delete;
    VDeviceBase &operator=(const VDeviceBase &) = delete;
    virtual ~VDeviceBase();

    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={}) override;

    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const override
    {
        // Return Expected for future functionality
        std::vector<std::reference_wrapper<Device>> devices_refs;
        for (const auto &pair : m_devices) {
            auto &device = pair.second;
            devices_refs.push_back(*device);
        }
        return devices_refs;
    }

    virtual Expected<std::vector<std::string>> get_physical_devices_ids() const override
    {
        std::vector<std::string> device_ids;
        device_ids.reserve(m_devices.size());
        for (const auto &pair : m_devices) {
            auto &id = pair.first;
            device_ids.push_back(id);
        }
        return device_ids;
    }

    const CoreOpsSchedulerPtr &core_ops_scheduler()
    {
        return m_core_ops_scheduler;
    }

    // Currently only homogeneous vDevice is allow (= all devices are from the same type)
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() const override;

    virtual hailo_status dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction) override
    {
        for (const auto &pair : m_devices) {
            auto &device = pair.second;
            const auto status = device->dma_map(address, size, direction);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

    virtual hailo_status dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction) override
    {
        hailo_status status = HAILO_SUCCESS;
        for (const auto &pair : m_devices) {
            auto &device = pair.second;
            // Best effort, propagate first error
            const auto unmap_status = device->dma_unmap(address, size, direction);
            if (HAILO_SUCCESS != unmap_status) {
                LOGGER__ERROR("Failed unmapping user buffer {} with status {}", address, unmap_status);
                if (HAILO_SUCCESS == status) {
                    status = unmap_status;
                }
            }
        }

        return status;
    }

    virtual hailo_status dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override
    {
        for (const auto &pair : m_devices) {
            auto &device = pair.second;
            const auto status = device->dma_map_dmabuf(dmabuf_fd, size, direction);
            CHECK_SUCCESS(status);
        }
        return HAILO_SUCCESS;
    }

    virtual hailo_status dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override
    {
        hailo_status status = HAILO_SUCCESS;
        for (const auto &pair : m_devices) {
            auto &device = pair.second;
            // Best effort, propagate first error
            const auto unmap_status = device->dma_unmap_dmabuf(dmabuf_fd, size, direction);
            if (HAILO_SUCCESS != unmap_status) {
                LOGGER__ERROR("Failed unmapping dmabuf {} with status {}", dmabuf_fd, unmap_status);
                if (HAILO_SUCCESS == status) {
                    status = unmap_status;
                }
            }
        }

        return status;
    }

    static Expected<HailoRTDriver::AcceleratorType> get_accelerator_type(hailo_device_id_t *device_ids, size_t device_count);
    static hailo_status validate_params(const hailo_vdevice_params_t &params);
    static Expected<bool> do_device_ids_contain_eth(const hailo_vdevice_params_t &params);

private:
    VDeviceBase(const hailo_vdevice_params_t &params, std::map<device_id_t, std::unique_ptr<Device>> &&devices, CoreOpsSchedulerPtr core_ops_scheduler,
        const std::string &unique_vdevice_hash="") : VDevice(params),
            m_devices(std::move(devices)), m_core_ops_scheduler(core_ops_scheduler), m_next_core_op_handle(0), m_unique_vdevice_hash(unique_vdevice_hash)
        {}

    static Expected<std::map<device_id_t, std::unique_ptr<Device>>> create_devices(const hailo_vdevice_params_t &params);
    static Expected<std::vector<std::string>> get_device_ids(const hailo_vdevice_params_t &params);
    Expected<NetworkGroupsParamsMap> create_local_config_params(Hef &hef, const NetworkGroupsParamsMap &configure_params);
    Expected<std::shared_ptr<VDeviceCoreOp>> create_vdevice_core_op(Hef &hef,
        const std::pair<const std::string, ConfigureNetworkParams> &params);
    Expected<std::shared_ptr<CoreOp>> create_physical_core_op(Device &device, Hef &hef, const std::string &core_op_name,
        const ConfigureNetworkParams &params);
    bool should_use_multiplexer();
    vdevice_core_op_handle_t allocate_core_op_handle();

    std::map<device_id_t, std::unique_ptr<Device>> m_devices;
    CoreOpsSchedulerPtr m_core_ops_scheduler;
    std::vector<std::shared_ptr<VDeviceCoreOp>> m_vdevice_core_ops;
    ActiveCoreOpHolder m_active_core_op_holder;
    vdevice_core_op_handle_t m_next_core_op_handle;
    const std::string m_unique_vdevice_hash; // Used to identify this vdevice in the monitor. consider removing - TODO (HRT-8835)
    std::mutex m_mutex;
};

class VDeviceHandle : public VDevice
{
public:
    static Expected<std::unique_ptr<VDevice>> create(const hailo_vdevice_params_t &params);

    VDeviceHandle(VDeviceHandle &&) = delete;
    VDeviceHandle(const VDeviceHandle &) = delete;
    VDeviceHandle &operator=(VDeviceHandle &&) = delete;
    VDeviceHandle &operator=(const VDeviceHandle &) = delete;
    virtual ~VDeviceHandle();

    Expected<ConfiguredNetworkGroupVector> configure(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={}) override;

    Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const override;
    Expected<std::vector<std::string>> get_physical_devices_ids() const override;
    Expected<hailo_stream_interface_t> get_default_streams_interface() const override;
    Expected<std::shared_ptr<InferModel>> create_infer_model(const std::string &hef_path,
        const std::string &name = "") override;
    virtual hailo_status dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;

private:
    VDeviceHandle(const hailo_vdevice_params_t &params, uint32_t handle);
    uint32_t m_handle;
};

} /* namespace hailort */

#endif /* _HAILO_DEVICE_INTERNAL_HPP_ */
