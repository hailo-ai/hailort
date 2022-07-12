/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file core_stream.cpp
 **/

#include "core_stream.hpp"
#include "control.hpp"

namespace hailort
{

Expected<std::unique_ptr<CoreInputStream>> CoreInputStream::create(Device &device,
    std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer,
    uint16_t batch_size, EventPtr network_group_activated_event)
{
    hailo_status status = HAILO_UNINITIALIZED;

    CHECK_AS_EXPECTED(device.get_type() == Device::Type::CORE, HAILO_INTERNAL_FAILURE,
        "Invalid device type");

    CoreDevice *core_device = reinterpret_cast<CoreDevice*>(&device);
    std::unique_ptr<CoreInputStream> local_stream(new (std::nothrow) CoreInputStream(*core_device,
        std::move(channel), edge_layer, std::move(network_group_activated_event), batch_size,
        DEFAULT_TRANSFER_TIMEOUT, status));
    CHECK((nullptr != local_stream), make_unexpected(HAILO_OUT_OF_HOST_MEMORY));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_stream;
}

CoreInputStream::CoreInputStream(
    CoreDevice &device,
    std::shared_ptr<VdmaChannel> channel,
    const LayerInfo &edge_layer,
    EventPtr network_group_activated_event,
    uint16_t batch_size,
    const std::chrono::milliseconds &transfer_timeout,
    hailo_status &status) :
        VdmaInputStream(device, std::move(channel), edge_layer, network_group_activated_event,
            batch_size, transfer_timeout, HAILO_STREAM_INTERFACE_CORE, status)
{}

Expected<std::unique_ptr<CoreOutputStream>> CoreOutputStream::create(Device &device,
    std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, 
    uint16_t batch_size, EventPtr network_group_activated_event)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CHECK_AS_EXPECTED(device.get_type() == Device::Type::CORE, HAILO_INTERNAL_FAILURE,
        "Invalid device type");

    CoreDevice *core_device = reinterpret_cast<CoreDevice*>(&device);
    std::unique_ptr<CoreOutputStream> local_stream(new (std::nothrow) CoreOutputStream(*core_device,
        std::move(channel), edge_layer, std::move(network_group_activated_event),
        batch_size, DEFAULT_TRANSFER_TIMEOUT, status));
    CHECK((nullptr != local_stream), make_unexpected(HAILO_OUT_OF_HOST_MEMORY));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return local_stream;
}

CoreOutputStream::CoreOutputStream(
    CoreDevice &device,
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
