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

#include "stream_internal.hpp"
#include "vdma_device.hpp"
#include "vdma_channel.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"

namespace hailort
{
constexpr std::chrono::seconds VDMA_FLUSH_TIMEOUT(10);

class VdmaInputStream : public InputStreamBase {
public:
    VdmaInputStream(VdmaInputStream &&other);
    virtual ~VdmaInputStream();

    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual hailo_status flush() override;
    hailo_status write_buffer_only(const MemoryView &buffer);
    Expected<PendingBufferState> send_pending_buffer();
    uint16_t get_dynamic_batch_size() const;
    const char* get_dev_id() const;

protected:
    VdmaInputStream(VdmaDevice &device, std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer,
                    EventPtr network_group_activated_event, uint16_t batch_size,
                    std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t stream_interface,
                    hailo_status &status);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;

    VdmaDevice *m_device;
    std::shared_ptr<VdmaChannel> m_channel;

private:
    hailo_status set_dynamic_batch_size(uint16_t dynamic_batch_size);

    bool is_stream_activated;
    std::chrono::milliseconds m_channel_timeout;
    const uint16_t m_max_batch_size;
    uint16_t m_dynamic_batch_size;
};

class VdmaOutputStream : public OutputStreamBase {
public:
    VdmaOutputStream(VdmaOutputStream &&other);
    virtual ~VdmaOutputStream();

    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    uint16_t get_dynamic_batch_size() const;
    const char* get_dev_id() const;

protected:
    VdmaOutputStream(VdmaDevice &device, std::shared_ptr<VdmaChannel> channel, const LayerInfo &edge_layer,
                     EventPtr network_group_activated_event, uint16_t batch_size,
                     std::chrono::milliseconds transfer_timeout, hailo_status &status);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer);

    VdmaDevice *m_device;
    std::shared_ptr<VdmaChannel> m_channel;

private:
    hailo_status read_all(MemoryView &buffer) override;
    static uint32_t get_transfer_size(const hailo_stream_info_t &stream_info);
    hailo_status set_dynamic_batch_size(uint16_t dynamic_batch_size);

    bool is_stream_activated;
    std::chrono::milliseconds m_transfer_timeout;
    const uint16_t m_max_batch_size;
    uint16_t m_dynamic_batch_size;
    const uint32_t m_transfer_size;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_STREAM_HPP_ */
