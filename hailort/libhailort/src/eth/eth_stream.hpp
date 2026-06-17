/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file eth_stream.hpp
 * @brief EthernetInputStream / EthernetOutputStream classes.
 *
 * DEPRECATED: ethernet/UDP firmware control is no longer supported. These
 * classes are retained as header-only stubs so that legacy call sites continue
 * to compile; every method now returns HAILO_NOT_SUPPORTED. They will be
 * removed in a future release.
 **/

#ifndef HAILO_ETH_STREAM_H_
#define HAILO_ETH_STREAM_H_

#include "hailo/hailort.h"
#include "hailo/hef.hpp"
#include "hailo/device.hpp"
#include "hailo/event.hpp"

#include "eth/udp.hpp"
#include "stream_common/stream_internal.hpp"

#if defined(__GNUC__)
#include "common/os/posix/traffic_control.hpp"
#endif


namespace hailort
{

// DEPRECATED: ethernet/UDP fw-control is no longer supported. Configuration
// structs and stream classes below are stubs retained for source compatibility.
typedef struct {
    uint16_t max_payload_size;
    bool use_dataflow_padding;
    bool is_sync_enabled;
    uint32_t frames_per_sync;
    uint32_t packets_per_frame;
    uint16_t sync_size;
    uint32_t buffers_threshold;
} hailo_stream_eth_input_configuration_t;

typedef struct {
    uint16_t max_payload_size;
    bool is_sync_enabled;
    uint32_t buffers_threshold;
} hailo_stream_eth_output_configuration_t;

// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class EthernetInputStream : public InputStreamBase {
private:
    hailo_status set_timeout(std::chrono::milliseconds timeout) override
    {
        (void)timeout;
        return HAILO_NOT_SUPPORTED;
    }

protected:
    virtual hailo_status eth_stream__write_with_remainder(const void *buffer, size_t offset, size_t size, size_t remainder_size)
    {
        (void)buffer;
        (void)offset;
        (void)size;
        (void)remainder_size;
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status write_impl(const MemoryView &buffer) override
    {
        (void)buffer;
        return HAILO_NOT_SUPPORTED;
    }

public:
    EthernetInputStream(Device &device, Udp &&udp, EventPtr &&core_op_activated_event, const LayerInfo &layer_info, hailo_status &status) :
        InputStreamBase(layer_info, std::move(core_op_activated_event), status)
    {
        (void)device;
        (void)udp;
        status = HAILO_NOT_SUPPORTED;
    }

    virtual ~EthernetInputStream() = default;

    static Expected<std::unique_ptr<EthernetInputStream>> create(Device &device,
        const LayerInfo &edge_layer, const hailo_eth_input_stream_params_t &params, EventPtr core_op_activated_event)
    {
        (void)device;
        (void)edge_layer;
        (void)params;
        (void)core_op_activated_event;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override
    {
        (void)buffer_mode;
        return HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status activate_stream() override { return HAILO_NOT_SUPPORTED; }
    virtual hailo_status deactivate_stream() override { return HAILO_NOT_SUPPORTED; }
    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_ETH; }
    virtual std::chrono::milliseconds get_timeout() const override { return std::chrono::milliseconds(0); }
    virtual hailo_status abort_impl() override { return HAILO_NOT_SUPPORTED; }
    virtual hailo_status clear_abort_impl() override { return HAILO_NOT_SUPPORTED; }
};

// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class EthernetInputStreamRateLimited : public EthernetInputStream {
protected:
    const uint32_t rate_bytes_per_sec;

public:
    EthernetInputStreamRateLimited(Device &device, Udp &&udp, EventPtr &&core_op_activated_event,
        uint32_t rate_bytes_per_sec, const LayerInfo &layer_info, hailo_status &status) :
        EthernetInputStream(device, std::move(udp), std::move(core_op_activated_event), layer_info, status),
        rate_bytes_per_sec(rate_bytes_per_sec)
    {
        status = HAILO_NOT_SUPPORTED;
    }

    virtual ~EthernetInputStreamRateLimited() = default;
};

// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class TokenBucketEthernetInputStream : public EthernetInputStreamRateLimited {

protected:
    virtual hailo_status eth_stream__write_with_remainder(const void *buffer, size_t offset, size_t size, size_t remainder_size) override
    {
        (void)buffer;
        (void)offset;
        (void)size;
        (void)remainder_size;
        return HAILO_NOT_SUPPORTED;
    }

public:
    TokenBucketEthernetInputStream(Device &device, Udp &&udp, EventPtr &&core_op_activated_event,
        uint32_t rate_bytes_per_sec, const LayerInfo &layer_info, hailo_status &status) :
        EthernetInputStreamRateLimited(device, std::move(udp), std::move(core_op_activated_event),
            rate_bytes_per_sec, layer_info, status)
    {
        status = HAILO_NOT_SUPPORTED;
    }

    virtual ~TokenBucketEthernetInputStream() = default;
};


#if defined(__GNUC__)
// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class TrafficControlEthernetInputStream : public EthernetInputStreamRateLimited {
public:
    static Expected<std::unique_ptr<TrafficControlEthernetInputStream>> create(Device &device, Udp &&udp,
        EventPtr &&core_op_activated_event, uint32_t rate_bytes_per_sec, const LayerInfo &layer_info)
    {
        (void)device;
        (void)udp;
        (void)core_op_activated_event;
        (void)rate_bytes_per_sec;
        (void)layer_info;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual ~TrafficControlEthernetInputStream() = default;

private:
    TrafficControlEthernetInputStream(Device &device, Udp &&udp, EventPtr &&core_op_activated_event,
        uint32_t rate_bytes_per_sec, TrafficControl &&tc, const LayerInfo &layer_info, hailo_status &status) :
        EthernetInputStreamRateLimited(device, std::move(udp), std::move(core_op_activated_event),
            rate_bytes_per_sec, layer_info, status)
    {
        (void)tc;
        status = HAILO_NOT_SUPPORTED;
    }
};
#endif

// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class EthernetOutputStream : public OutputStreamBase {
private:
    EthernetOutputStream(Device &device, const LayerInfo &edge_layer, Udp &&udp, EventPtr &&core_op_activated_event, hailo_status &status) :
        OutputStreamBase(edge_layer, std::move(core_op_activated_event), status)
    {
        (void)device;
        (void)udp;
        status = HAILO_NOT_SUPPORTED;
    }

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override
    {
        (void)buffer_mode;
        return HAILO_NOT_SUPPORTED;
    }

    hailo_status read_impl(MemoryView buffer) override
    {
        (void)buffer;
        return HAILO_NOT_SUPPORTED;
    }

public:
    virtual ~EthernetOutputStream() = default;

    static Expected<std::unique_ptr<EthernetOutputStream>> create(Device &device, const LayerInfo &edge_layer,
        const hailo_eth_output_stream_params_t &params, EventPtr core_op_activated_event)
    {
        (void)device;
        (void)edge_layer;
        (void)params;
        (void)core_op_activated_event;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual hailo_status activate_stream() override { return HAILO_NOT_SUPPORTED; }
    virtual hailo_status deactivate_stream() override { return HAILO_NOT_SUPPORTED; }
    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_ETH; }
    virtual std::chrono::milliseconds get_timeout() const override { return std::chrono::milliseconds(0); }
    virtual hailo_status abort_impl() override { return HAILO_NOT_SUPPORTED; }
    virtual hailo_status clear_abort_impl() override { return HAILO_NOT_SUPPORTED; }
};

} /* namespace hailort */

#endif /* HAILO_ETH_STREAM_H_ */
