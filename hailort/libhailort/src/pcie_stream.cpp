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
    uint8_t channel_index,
    const LayerInfo &edge_layer,
    EventPtr network_group_activated_event,
    uint16_t batch_size,
    LatencyMeterPtr latency_meter,
    const std::chrono::milliseconds &transfer_timeout,
    hailo_status &status) :
        VdmaInputStream(device, channel_index, edge_layer, network_group_activated_event,
            batch_size, latency_meter, transfer_timeout, HAILO_STREAM_INTERFACE_PCIE, status)
    {}

Expected<std::unique_ptr<PcieInputStream>> PcieInputStream::create(Device &device,
    uint8_t channel_index, const LayerInfo &edge_layer, uint16_t batch_size,
    EventPtr network_group_activated_event, LatencyMeterPtr latency_meter)
{
    hailo_status status = HAILO_UNINITIALIZED;

    PcieDevice *pcie_device = reinterpret_cast<PcieDevice*>(&device);
    std::unique_ptr<PcieInputStream> local_stream(new (std::nothrow) PcieInputStream(*pcie_device,
        channel_index, edge_layer, std::move(network_group_activated_event), batch_size,
        latency_meter, DEFAULT_TRANSFER_TIMEOUT, status));
    CHECK((nullptr != local_stream), make_unexpected(HAILO_OUT_OF_HOST_MEMORY));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_stream;
}

Expected<std::unique_ptr<PcieOutputStream>> PcieOutputStream::create(Device &device,
    uint8_t channel_index, const LayerInfo &edge_layer, uint16_t batch_size,
    EventPtr network_group_activated_event, LatencyMeterPtr latency_meter)
{
    hailo_status status = HAILO_UNINITIALIZED;
    PcieDevice *pcie_device = reinterpret_cast<PcieDevice*>(&device);

    std::unique_ptr<PcieOutputStream> local_stream(new (std::nothrow) PcieOutputStream(*pcie_device,
        channel_index, edge_layer, std::move(network_group_activated_event),
        batch_size, latency_meter, DEFAULT_TRANSFER_TIMEOUT, status));
    CHECK((nullptr != local_stream), make_unexpected(HAILO_OUT_OF_HOST_MEMORY));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_stream;
}

PcieOutputStream::PcieOutputStream(
    PcieDevice &device,
    uint8_t channel_index,
    const LayerInfo &edge_layer,
    EventPtr network_group_activated_event,
    uint16_t batch_size,
    LatencyMeterPtr latency_meter,
    const std::chrono::milliseconds &transfer_timeout,
    hailo_status &status) :
        VdmaOutputStream(device, channel_index, edge_layer,
            network_group_activated_event, batch_size, latency_meter, transfer_timeout, status)
    {}

} /* namespace hailort */
