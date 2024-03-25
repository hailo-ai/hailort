/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_device.hpp
 * @brief Base class for devices that uses vdma and comunicate using HailoRTDriver
 *
 **/

#ifndef HAILO_VDMA_DEVICE_H_
#define HAILO_VDMA_DEVICE_H_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "device_common/device_internal.hpp"
#include "network_group/network_group_internal.hpp"
#include "vdma/channel/interrupts_dispatcher.hpp"
#include "vdma/channel/transfer_launcher.hpp"
#include "vdma/driver/hailort_driver.hpp"


namespace hailort
{

class VdmaDevice : public DeviceBase {
public:
    static Expected<std::unique_ptr<VdmaDevice>> create(const std::string &device_id);

    virtual ~VdmaDevice();

    virtual hailo_status wait_for_wakeup() override;
    virtual void increment_control_sequence() override;
    virtual void shutdown_core_ops() override;
    virtual hailo_reset_device_mode_t get_default_reset_mode() override;
    hailo_status mark_as_used();
    virtual Expected<size_t> read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id) override;

    HailoRTDriver &get_driver()
    {
        return std::ref(*m_driver);
    };

    virtual const char* get_dev_id() const override final
    {
        // m_driver.device_id() is reference. Hence, returning c_str is safe.
        return m_driver->device_id().c_str();
    };

    ExpectedRef<vdma::InterruptsDispatcher> get_vdma_interrupts_dispatcher();
    ExpectedRef<vdma::TransferLauncher> get_vdma_transfer_launcher();

    virtual hailo_status dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;

protected:
    VdmaDevice(std::unique_ptr<HailoRTDriver> &&driver, Type type);

    virtual Expected<D2H_EVENT_MESSAGE_t> read_notification() override;
    virtual hailo_status disable_notifications() override;
    virtual hailo_status fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id) override;
    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef, const NetworkGroupsParamsMap &configure_params) override;

    std::unique_ptr<HailoRTDriver> m_driver;
    // TODO - HRT-13234, move to DeviceBase
    std::vector<std::shared_ptr<CoreOp>> m_core_ops;
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> m_network_groups; // TODO: HRT-9547 - Remove when ConfiguredNetworkGroup will be kept in global context

    // The vdma interrupts dispatcher contains a callback with a reference to the current activated network group
    // (reference to the ResourcesManager). Hence, it must be destroyed before the networks groups are destroyed.
    std::unique_ptr<vdma::InterruptsDispatcher> m_vdma_interrupts_dispatcher;
    std::unique_ptr<vdma::TransferLauncher> m_vdma_transfer_launcher;

    ActiveCoreOpHolder m_active_core_op_holder;
    bool m_is_configured;

private:
    Expected<std::shared_ptr<ConfiguredNetworkGroup>> create_configured_network_group(
        std::vector<std::shared_ptr<CoreOpMetadata>> &core_ops,
        Hef &hef, const ConfigureNetworkParams &config_params,
        uint8_t network_group_index);
    hailo_status clear_configured_apps();
    Expected<ConfiguredNetworkGroupVector> create_networks_group_vector(Hef &hef, const NetworkGroupsParamsMap &configure_params);
    Expected<std::vector<std::shared_ptr<CoreOpMetadata>>> create_core_ops_metadata(Hef &hef, const std::string &network_group_name,
        uint32_t partial_clusters_layout_bitmap);
};

} /* namespace hailort */

#endif /* HAILO_VDMA_DEVICE_H_ */
