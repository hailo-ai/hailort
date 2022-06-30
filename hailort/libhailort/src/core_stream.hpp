/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file core_stream.hpp
 * @brief Stream object for Core device
 **/

#ifndef _HAILO_CORE_STREAM_HPP_
#define _HAILO_CORE_STREAM_HPP_

#include "vdma_stream.hpp"
#include "core_device.hpp"


namespace hailort
{

class CoreInputStream : public VdmaInputStream {
public:
    CoreInputStream(CoreInputStream &&other) = default;
    virtual ~CoreInputStream() = default;

    static Expected<std::unique_ptr<CoreInputStream>> create(Device &device,
        std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, uint16_t batch_size, 
        EventPtr network_group_activated_event);

    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_CORE; }

private:
    CoreInputStream(
        CoreDevice &device,
        std::shared_ptr<VdmaChannel> channel,
        const LayerInfo &edge_layer,
        EventPtr network_group_activated_event,
        uint16_t batch_size,
        const std::chrono::milliseconds &transfer_timeout,
        hailo_status &status);
};

class CoreOutputStream : public VdmaOutputStream {
public:
    CoreOutputStream(CoreOutputStream &&other) = default;
    virtual ~CoreOutputStream() = default;

    static Expected<std::unique_ptr<CoreOutputStream>> create(Device &device,
        std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, uint16_t batch_size, 
        EventPtr network_group_activated_event);

    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_CORE; }

private:
    explicit CoreOutputStream(
        CoreDevice &device,
        std::shared_ptr<VdmaChannel> channel,
        const LayerInfo &edge_layer,
        EventPtr network_group_activated_event,
        uint16_t batch_size,
        const std::chrono::milliseconds &transfer_timeout,
        hailo_status &status);
};

} /* namespace hailort */

#endif /* _HAILO_CORE_STREAM_HPP_ */
