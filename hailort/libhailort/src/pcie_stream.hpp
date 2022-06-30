/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_stream.hpp
 * @brief Stream object for PCIe device
 **/

#ifndef _HAILO_PCIE_STREAM_H_
#define _HAILO_PCIE_STREAM_H_

#include "vdma_stream.hpp"
#include "pcie_device.hpp"

namespace hailort
{

class PcieInputStream : public VdmaInputStream {
public:
    PcieInputStream(PcieInputStream &&other) = default;
    virtual ~PcieInputStream() = default;

    static Expected<std::unique_ptr<PcieInputStream>> create(Device &device,
        std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, uint16_t batch_size, 
        EventPtr network_group_activated_event);

    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_PCIE; }

private:
    PcieInputStream(
        PcieDevice &device,
        std::shared_ptr<VdmaChannel> channel,
        const LayerInfo &edge_layer,
        EventPtr network_group_activated_event,
        uint16_t batch_size,
        const std::chrono::milliseconds &transfer_timeout,
        hailo_status &status);

    friend class VDeviceInputStream;
};

class PcieOutputStream : public VdmaOutputStream {
public:
    PcieOutputStream(PcieOutputStream &&other) = default;
    virtual ~PcieOutputStream() = default;

    static Expected<std::unique_ptr<PcieOutputStream>> create(Device &device,
        std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer, uint16_t batch_size, 
        EventPtr network_group_activated_event);

    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_PCIE; }

    friend class VDeviceOutputStream;

private:
    explicit PcieOutputStream(
        PcieDevice &device,
        std::shared_ptr<VdmaChannel> channel,
        const LayerInfo &edge_layer,
        EventPtr network_group_activated_event,
        uint16_t batch_size,
        const std::chrono::milliseconds &transfer_timeout,
        hailo_status &status);
};

} /* namespace hailort */

#endif /* _HAILO_PCIE_STREAM_H_ */
