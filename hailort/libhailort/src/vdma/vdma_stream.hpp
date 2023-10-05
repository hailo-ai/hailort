/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_stream.hpp
 * @brief Stream object over vDMA channel
 **/

#ifndef _HAILO_VDMA_STREAM_HPP_
#define _HAILO_VDMA_STREAM_HPP_

#include "hailo/expected.hpp"

#include "stream_common/async_stream_base.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/channel/boundary_channel.hpp"


namespace hailort
{

class VdmaInputStream : public AsyncInputStreamBase {
public:

    static Expected<std::shared_ptr<VdmaInputStream>> create(hailo_stream_interface_t interface,
        VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
        EventPtr core_op_activated_event);

    VdmaInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                    EventPtr core_op_activated_event, hailo_stream_interface_t stream_interface, hailo_status &status);
    virtual ~VdmaInputStream();

    virtual hailo_stream_interface_t get_interface() const override;

    virtual vdevice_core_op_handle_t get_vdevice_core_op_handle() override;

    virtual void set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle) override;

private:
    Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() override;
    virtual size_t get_max_ongoing_transfers() const override;
    virtual hailo_status write_async_impl(TransferRequest &&transfer_request) override;
    virtual hailo_status activate_stream_impl() override;
    virtual hailo_status deactivate_stream_impl() override;

    VdmaDevice &m_device;
    vdma::BoundaryChannelPtr m_channel;
    const hailo_stream_interface_t m_interface;
    vdevice_core_op_handle_t m_core_op_handle;
};

class VdmaOutputStream : public AsyncOutputStreamBase
{
public:
    static Expected<std::shared_ptr<VdmaOutputStream>> create(hailo_stream_interface_t interface,
        VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
        EventPtr core_op_activated_event);

    VdmaOutputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                     EventPtr core_op_activated_event, hailo_stream_interface_t interface,
                     hailo_status &status);
    virtual ~VdmaOutputStream();

    virtual hailo_stream_interface_t get_interface() const override;

    virtual Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() override;
    virtual size_t get_max_ongoing_transfers() const override;
    virtual hailo_status read_async_impl(TransferRequest &&transfer_request) override;
    virtual hailo_status activate_stream_impl() override;
    virtual hailo_status deactivate_stream_impl() override;
private:
    static uint32_t get_transfer_size(const hailo_stream_info_t &stream_info, const LayerInfo &layer_info);

    VdmaDevice &m_device;
    vdma::BoundaryChannelPtr m_channel;
    const hailo_stream_interface_t m_interface;
    const uint32_t m_transfer_size;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_STREAM_HPP_ */
