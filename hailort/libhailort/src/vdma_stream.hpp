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
    hailo_status send_pending_buffer();

    uint16_t get_batch_size() const
    {
        return m_batch_size;
    }

    const char* get_dev_id() const
    {
        return m_device->get_dev_id();
    }

protected:
    VdmaInputStream(VdmaDevice &device, uint8_t channel_index, const LayerInfo &edge_layer,
                    EventPtr network_group_activated_event, uint16_t batch_size, LatencyMeterPtr latency_meter,
                    std::chrono::milliseconds transfer_timeout, hailo_stream_interface_t stream_interface,
                    hailo_status &status);

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;

    VdmaDevice *m_device;
    const uint8_t m_channel_index;
    std::unique_ptr<VdmaChannel> m_channel;

private:
    hailo_status config_stream();

    bool is_stream_activated;
    std::chrono::milliseconds m_channel_timeout;
    LatencyMeterPtr m_latency_meter;
    uint16_t m_batch_size;
};

class VdmaOutputStream : public OutputStreamBase {
public:
    VdmaOutputStream(VdmaOutputStream &&other);
    virtual ~VdmaOutputStream();

    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

    uint16_t get_batch_size() const
    {
        return m_batch_size;
    }

    const char* get_dev_id() const
    {
        return m_device->get_dev_id();
    }

protected:
    VdmaOutputStream(VdmaDevice &device, uint8_t channel_index, const LayerInfo &edge_layer,
                     EventPtr network_group_activated_event, uint16_t batch_size, LatencyMeterPtr latency_meter,
                     std::chrono::milliseconds transfer_timeout, hailo_status &status);

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer);

    VdmaDevice *m_device;
    const uint8_t m_channel_index;
    std::unique_ptr<VdmaChannel> m_channel;

private:
    hailo_status config_stream();
    hailo_status read_all(MemoryView &buffer) override;
    static uint32_t get_transfer_size(const hailo_stream_info_t &stream_info);

    bool is_stream_activated;
    std::chrono::milliseconds m_transfer_timeout;
    LatencyMeterPtr m_latency_meter;
    uint16_t m_batch_size;
    const uint32_t m_transfer_size;
};


} /* namespace hailort */

#endif /* _HAILO_VDMA_STREAM_HPP_ */
