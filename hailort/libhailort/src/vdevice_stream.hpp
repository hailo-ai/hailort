/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream.hpp
 * @brief Internal stream implementation for VDevice - holds PcieStream per physical device
 *
 * TODO: doc
 **/

#ifndef HAILO_VDEVICE_STREAM_HPP_
#define HAILO_VDEVICE_STREAM_HPP_

#include "stream_internal.hpp"
#include "hailo/hailort.h"
#include "vdevice_internal.hpp"
#include "pcie_device.hpp"
#include "pcie_stream.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

class VDeviceInputStream : public InputStreamBase {
public:
    VDeviceInputStream(VDeviceInputStream &&other) :
        InputStreamBase(std::move(other)),
        m_network_group_name(std::move(other.m_network_group_name)),
        m_network_group_scheduler(std::move(other.m_network_group_scheduler)),
        m_devices(std::move(other.m_devices)),
        m_streams(std::move(other.m_streams)),
        m_is_stream_activated(std::exchange(other.m_is_stream_activated, false)),
        m_next_transfer_stream_index(other.m_next_transfer_stream_index),
        m_acc_frames(other.m_acc_frames)
    {}

    virtual ~VDeviceInputStream();

    static Expected<std::unique_ptr<VDeviceInputStream>> create(std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
        const LayerInfo &edge_layer, const std::string &stream_name, const std::string &network_group_name, EventPtr network_group_activated_event,
        NetworkGroupSchedulerWeakPtr network_group_scheduler, LatencyMeterPtr latency_meter = nullptr);

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_PCIE; }
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;

private:
    explicit VDeviceInputStream(
        std::vector<PcieDevice*> devices,
        std::vector<std::unique_ptr<PcieInputStream>> &&streams,
        const std::string &network_group_name,
        EventPtr &&network_group_activated_event,
        const LayerInfo &layer_info,
        NetworkGroupSchedulerWeakPtr network_group_scheduler,
        hailo_status &status) :
            InputStreamBase(layer_info, HAILO_STREAM_INTERFACE_PCIE, std::move(network_group_activated_event), status),
            m_network_group_name(network_group_name),
            m_network_group_scheduler(network_group_scheduler),
            m_devices(devices),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream_index(0),
            m_acc_frames(0)
    {}

    hailo_status set_timeout(std::chrono::milliseconds timeout);
    virtual hailo_status flush() override;

    static Expected<std::unique_ptr<VDeviceInputStream>> create_input_stream_from_net_group(
        std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
        const LayerInfo &edge_layer, const std::string &stream_name, const std::string &network_group_name,
        EventPtr &&network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler, LatencyMeterPtr latency_meter);

    std::string m_network_group_name;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;
    std::vector<PcieDevice*> m_devices;
    std::vector<std::unique_ptr<PcieInputStream>> m_streams;
    bool m_is_stream_activated;
    uint32_t m_next_transfer_stream_index;
    uint32_t m_acc_frames;
};

class VDeviceOutputStream : public OutputStreamBase {
public:
    VDeviceOutputStream(VDeviceOutputStream &&other) :
        OutputStreamBase(std::move(other)),
        m_network_group_name(std::move(other.m_network_group_name)),
        m_network_group_scheduler(std::move(other.m_network_group_scheduler)),
        m_devices(std::move(other.m_devices)),
        m_streams(std::move(other.m_streams)),
        m_is_stream_activated(std::exchange(other.m_is_stream_activated, false)),
        m_next_transfer_stream_index(other.m_next_transfer_stream_index),
        m_acc_frames(other.m_acc_frames)
    {}

    virtual ~VDeviceOutputStream();

    static Expected<std::unique_ptr<VDeviceOutputStream>> create(std::vector<std::shared_ptr<ResourcesManager>> &resources_managers,
        const LayerInfo &edge_layer, const std::string &stream_name, const std::string &network_group_name,
        EventPtr network_group_activated_event, NetworkGroupSchedulerWeakPtr network_group_scheduler, LatencyMeterPtr latency_meter = nullptr);

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_PCIE; }
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;

protected:
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer) override;

private:
    explicit VDeviceOutputStream(
        std::vector<PcieDevice*> devices,
        std::vector<std::unique_ptr<PcieOutputStream>> &&streams,
        const std::string &network_group_name,
        const LayerInfo &layer_info,
        EventPtr &&network_group_activated_event,
        NetworkGroupSchedulerWeakPtr network_group_scheduler,
        hailo_status &status) :
            OutputStreamBase(layer_info, std::move(network_group_activated_event), status),
            m_network_group_name(network_group_name),
            m_network_group_scheduler(network_group_scheduler),
            m_devices(devices),
            m_streams(std::move(streams)),
            m_is_stream_activated(false),
            m_next_transfer_stream_index(0),
            m_acc_frames(0)
    {}

    hailo_status set_timeout(std::chrono::milliseconds timeout);
    virtual hailo_status read_all(MemoryView &buffer) override;
    virtual hailo_status read(MemoryView buffer) override;
    
    static Expected<std::unique_ptr<VDeviceOutputStream>> create_output_stream_from_net_group(
        std::vector<std::shared_ptr<ResourcesManager>> &resources_managers, const LayerInfo &edge_layer,
        const std::string &stream_name, const std::string &network_group_name, EventPtr &&network_group_activated_event,
        NetworkGroupSchedulerWeakPtr network_group_scheduler, LatencyMeterPtr latency_meter);

    std::string m_network_group_name;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;
    std::vector<PcieDevice*> m_devices;
    std::vector<std::unique_ptr<PcieOutputStream>> m_streams;
    bool m_is_stream_activated;
    uint32_t m_next_transfer_stream_index;
    uint32_t m_acc_frames;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_STREAM_HPP_ */
