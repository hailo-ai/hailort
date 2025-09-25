/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

struct BounceBuffer {
    Buffer buffer_storage;
    DmaMappedBuffer mapping;
};
using BounceBufferPtr = std::shared_ptr<BounceBuffer>;

using BounceBufferQueue = SafeQueue<BounceBufferPtr>;
using BounceBufferQueuePtr = std::unique_ptr<BounceBufferQueue>;

class VdmaInputStream : public AsyncInputStreamBase {
public:

    static Expected<std::shared_ptr<VdmaInputStream>> create(hailo_stream_interface_t interface,
        VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
        EventPtr core_op_activated_event);

    VdmaInputStream(VdmaDevice &device, vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer,
                    EventPtr core_op_activated_event, hailo_stream_interface_t stream_interface,
                    BounceBufferQueuePtr &&bounce_buffers_pool, hailo_status &status);
    virtual ~VdmaInputStream();

    virtual hailo_stream_interface_t get_interface() const override;
    virtual void set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle) override;
    virtual hailo_status cancel_pending_transfers() override;
    virtual hailo_status bind_buffer(TransferRequest &&transfer_request) override final;

private:
    Expected<std::unique_ptr<StreamBufferPool>> allocate_buffer_pool() override;
    virtual size_t get_max_ongoing_transfers() const override;
    virtual hailo_status write_async_impl(TransferRequest &&transfer_request) override;
    virtual hailo_status activate_stream_impl() override;
    virtual hailo_status deactivate_stream_impl() override;

    static Expected<BounceBufferQueuePtr> init_dma_bounce_buffer_pool(VdmaDevice &device,
        vdma::BoundaryChannelPtr channel, const LayerInfo &edge_layer);
    Expected<TransferRequest> align_transfer_request(TransferRequest &&transfer_request);

    VdmaDevice &m_device;
    BounceBufferQueuePtr m_bounce_buffers_pool;

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

    virtual void set_vdevice_core_op_handle(vdevice_core_op_handle_t core_op_handle) override;

    virtual inline const char *get_device_id() override { return m_device.get_dev_id(); };
    // TODO - HRT-11739 - remove vdevice related members/functions (get/set_vdevice_core_op_handle)
    virtual inline vdevice_core_op_handle_t get_vdevice_core_op_handle() override { return m_core_op_handle; };
    virtual hailo_status cancel_pending_transfers() override;
    void set_d2h_callback(std::function<void(hailo_status)> callback);
    virtual hailo_status bind_buffer(TransferRequest &&transfer_request) override final;

private:
    static void default_d2h_callback(hailo_status) {};
    static uint32_t get_transfer_size(const hailo_stream_info_t &stream_info, const LayerInfo &layer_info);
    Expected<TransferRequest> align_transfer_request(TransferRequest &&transfer_request);
    Expected<TransferRequest> align_transfer_request_only_end(TransferRequest &&transfer_request);

    VdmaDevice &m_device;
    vdma::BoundaryChannelPtr m_channel;
    const hailo_stream_interface_t m_interface;
    const uint32_t m_transfer_size;
    vdevice_core_op_handle_t m_core_op_handle;
    std::function<void(hailo_status)> m_d2h_callback;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_STREAM_HPP_ */
