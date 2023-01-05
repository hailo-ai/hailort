/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_internal.hpp
 * @brief Class declaration for VDeviceBase that implements the basic VDevice "interface".
 *        Hence, the hiearchy is as follows:
 *
 * VDevice                  (External "interface")
 * |
 * |-- VDeviceHandle            (VDevice handle for a possibly shared VDeviceBase
 * |                         when hailort is running as single process)
 * |-- VDeviceClient            (VDevice client for a possibly shared VDeviceBase
 * |                         when hailort is running as a service)
 * |-- VDeviceBase          (Actual implementations)
 *     |
 *     |-- std::vector<VdmaDevice>
 **/

#ifndef _HAILO_VDEVICE_INTERNAL_HPP_
#define _HAILO_VDEVICE_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"
#include "vdma_device.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "context_switch/vdevice_network_group.hpp"
#include "network_group_scheduler.hpp"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "hailort_rpc_client.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS

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
    virtual ~VDeviceBase() = default;

    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={}) override;

    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const override
    {
        // Return Expected for future functionality
        std::vector<std::reference_wrapper<Device>> devices_refs;
        for (auto &device : m_devices) {
            devices_refs.push_back(*device);
        }
        return devices_refs;
    }

    virtual Expected<std::vector<std::string>> get_physical_devices_ids() const override
    {
        std::vector<std::string> device_ids;
        device_ids.reserve(m_devices.size());
        for (auto &device : m_devices) {
            device_ids.push_back(device.get()->get_dev_id());
        }
        return device_ids;
    }

    const NetworkGroupSchedulerPtr &network_group_scheduler()
    {
        return m_network_group_scheduler;
    }

    // Currently only homogeneous vDevice is allow (= all devices are from the same type)
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() const override;

    // TODO: Remove when feature becomes 'released'
    static bool enable_multi_device_schedeulr()
    {
        auto enable_multi_device_schedeulr_env = std::getenv(HAILO_ENABLE_MULTI_DEVICE_SCHEDULER);
        return ((nullptr != enable_multi_device_schedeulr_env) &&
            (strnlen(enable_multi_device_schedeulr_env, 2) == 1) && (strncmp(enable_multi_device_schedeulr_env, "1", 1) == 0));
    }

private:
    VDeviceBase(std::vector<std::unique_ptr<VdmaDevice>> &&devices, NetworkGroupSchedulerPtr network_group_scheduler) :
        m_devices(std::move(devices)), m_network_group_scheduler(network_group_scheduler), m_network_groups({})
        {}

    static Expected<std::vector<std::unique_ptr<VdmaDevice>>> create_devices(const hailo_vdevice_params_t &params);
    static Expected<std::vector<std::string>> get_device_ids(const hailo_vdevice_params_t &params);

    std::vector<std::unique_ptr<VdmaDevice>> m_devices;
    NetworkGroupSchedulerPtr m_network_group_scheduler;
    std::vector<std::shared_ptr<VDeviceNetworkGroup>> m_network_groups;

    std::mutex m_mutex;
};

#ifdef HAILO_SUPPORT_MULTI_PROCESS
class VDeviceClient : public VDevice
{
public:
    static Expected<std::unique_ptr<VDevice>> create(const hailo_vdevice_params_t &params);

    VDeviceClient(VDeviceClient &&) = delete;
    VDeviceClient(const VDeviceClient &) = delete;
    VDeviceClient &operator=(VDeviceClient &&) = delete;
    VDeviceClient &operator=(const VDeviceClient &) = delete;
    virtual ~VDeviceClient();

    Expected<ConfiguredNetworkGroupVector> configure(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={}) override;

    Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() const override;

    Expected<std::vector<std::string>> get_physical_devices_ids() const override;
    Expected<hailo_stream_interface_t> get_default_streams_interface() const override;

private:
    VDeviceClient(std::unique_ptr<HailoRtRpcClient> client, uint32_t handle);

    std::unique_ptr<HailoRtRpcClient> m_client;
    uint32_t m_handle;
};

#endif // HAILO_SUPPORT_MULTI_PROCESS

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

private:
    VDeviceHandle(uint32_t handle);
    uint32_t m_handle;
};

} /* namespace hailort */

#endif /* _HAILO_DEVICE_INTERNAL_HPP_ */
