/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_stream.cpp
 **/

#include "pcie_stream.hpp"

namespace hailort
{

PcieInputStream::PcieInputStream(
    PcieDevice &device,
    std::shared_ptr<VdmaChannel> channel,
    const LayerInfo &edge_layer,
    EventPtr network_group_activated_event,
    uint16_t batch_size,
    const std::chrono::milliseconds &transfer_timeout,
    hailo_status &status) :
        VdmaInputStream(device, std::move(channel), edge_layer, network_group_activated_event,
            batch_size, transfer_timeout, HAILO_STREAM_INTERFACE_PCIE, status)
    {}

Expected<std::unique_ptr<PcieInputStream>> PcieInputStream::create(Device &device,
    std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, 
    uint16_t batch_size, EventPtr network_group_activated_event)
{
    hailo_status status = HAILO_UNINITIALIZED;

    PcieDevice *pcie_device = reinterpret_cast<PcieDevice*>(&device);
    std::unique_ptr<PcieInputStream> local_stream(new (std::nothrow) PcieInputStream(*pcie_device,
        std::move(channel), edge_layer, std::move(network_group_activated_event), batch_size,
        DEFAULT_TRANSFER_TIMEOUT, status));
    CHECK((nullptr != local_stream), make_unexpected(HAILO_OUT_OF_HOST_MEMORY));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_stream;
}

Expected<std::unique_ptr<PcieOutputStream>> PcieOutputStream::create(Device &device,
    std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, uint16_t batch_size,
    EventPtr network_group_activated_event)
{
    hailo_status status = HAILO_UNINITIALIZED;
    PcieDevice *pcie_device = reinterpret_cast<PcieDevice*>(&device);

    std::unique_ptr<PcieOutputStream> local_stream(new (std::nothrow) PcieOutputStream(*pcie_device,
        std::move(channel), edge_layer, std::move(network_group_activated_event),
        batch_size, DEFAULT_TRANSFER_TIMEOUT, status));
    CHECK((nullptr != local_stream), make_unexpected(HAILO_OUT_OF_HOST_MEMORY));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_stream;
}

PcieOutputStream::PcieOutputStream(
    PcieDevice &device,
    std::shared_ptr<VdmaChannel> channel,
    const LayerInfo &edge_layer,
    EventPtr network_group_activated_event,
    uint16_t batch_size,
    const std::chrono::milliseconds &transfer_timeout,
    hailo_status &status) :
        VdmaOutputStream(device, std::move(channel), edge_layer,
            network_group_activated_event, batch_size, transfer_timeout, status)
    {}

} /* namespace hailort */
