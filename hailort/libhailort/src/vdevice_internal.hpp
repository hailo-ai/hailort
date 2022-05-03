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
 * └── VDeviceBase          (Actual implementations)
 *     |
 *     ├── std::vector<PcieDevice>
 **/

#ifndef _HAILO_VDEVICE_INTERNAL_HPP_
#define _HAILO_VDEVICE_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"
#include "pcie_device.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "network_group_scheduler.hpp"


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

    virtual Expected<std::vector<std::reference_wrapper<Device>>> get_physical_devices() override
    {
        // Return Expected for future functionality
        std::vector<std::reference_wrapper<Device>> devices_refs;
        for (auto &device : m_devices) {
            devices_refs.push_back(*device);
        }
        return devices_refs;
    }

    virtual Expected<std::vector<hailo_pcie_device_info_t>> get_physical_devices_infos() override
    {
        // Return Expected for future functionality
        std::vector<hailo_pcie_device_info_t> devices_infos;
        for (auto &device : m_devices) {
            devices_infos.push_back(device->get_device_info());
        }

        return devices_infos;
    }

    const NetworkGroupSchedulerPtr &network_group_scheduler()
    {
        return m_network_group_scheduler;
    }

private:
    VDeviceBase(std::vector<std::unique_ptr<PcieDevice>> &&devices, NetworkGroupSchedulerPtr network_group_scheduler) :
        m_devices(std::move(devices)), m_network_group_scheduler(network_group_scheduler)
        {}

    std::vector<std::unique_ptr<PcieDevice>> m_devices;
    std::unique_ptr<VdmaConfigManager> m_context_switch_manager;
    NetworkGroupSchedulerPtr m_network_group_scheduler;
};

} /* namespace hailort */

#endif /* _HAILO_DEVICE_INTERNAL_HPP_ */
