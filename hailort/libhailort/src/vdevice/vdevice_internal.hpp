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

#include "vdma/vdma_device.hpp"
#include "vdma/vdma_config_manager.hpp"
#include "vdevice/vdevice_core_op.hpp"
#include "vdevice/scheduler/scheduler.hpp"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "service/hailort_rpc_client.hpp"
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

    static hailo_status validate_params(const hailo_vdevice_params_t &params);

private:
    VDeviceBase(std::map<device_id_t, std::unique_ptr<Device>> &&devices, CoreOpsSchedulerPtr core_ops_scheduler) :
        m_devices(std::move(devices)), m_core_ops_scheduler(core_ops_scheduler)
        {}

    static Expected<std::map<device_id_t, std::unique_ptr<Device>>> create_devices(const hailo_vdevice_params_t &params);
    static Expected<std::vector<std::string>> get_device_ids(const hailo_vdevice_params_t &params);
    Expected<NetworkGroupsParamsMap> create_local_config_params(Hef &hef, const NetworkGroupsParamsMap &configure_params);
    Expected<std::shared_ptr<VDeviceCoreOp>> create_vdevice_network_group(Hef &hef,
        const std::pair<const std::string, ConfigureNetworkParams> &params, bool use_multiplexer);
    bool should_use_multiplexer(const ConfigureNetworkParams &params);

    std::map<device_id_t, std::unique_ptr<Device>> m_devices;
    CoreOpsSchedulerPtr m_core_ops_scheduler;
    std::vector<std::shared_ptr<VDeviceCoreOp>> m_vdevice_core_ops;
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> m_network_groups; // TODO: HRT-9547 - Remove when ConfiguredNetworkGroup will be kept in global context

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

    virtual hailo_status before_fork() override;
    virtual hailo_status after_fork_in_parent() override;
    virtual hailo_status after_fork_in_child() override;

private:
    VDeviceClient(std::unique_ptr<HailoRtRpcClient> client, uint32_t handle, std::vector<std::unique_ptr<hailort::Device>> &&devices);

    hailo_status create_client();

    std::unique_ptr<HailoRtRpcClient> m_client;
    uint32_t m_handle;
    std::vector<std::unique_ptr<Device>> m_devices;
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> m_network_groups;
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
