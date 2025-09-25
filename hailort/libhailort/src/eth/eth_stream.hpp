/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file eth_stream.hpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#ifndef HAILO_ETH_STREAM_H_
#define HAILO_ETH_STREAM_H_

#include "hailo/hailort.h"
#include "hailo/hef.hpp"
#include "hailo/device.hpp"
#include "hailo/event.hpp"

#include "eth/token_bucket.hpp"
#include "eth/udp.hpp"
#include "stream_common/stream_internal.hpp"

#if defined(__GNUC__)
#include "common/os/posix/traffic_control.hpp"
#endif


namespace hailort
{

// TODO: move those structs to hailort.h when implemented
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

class EthernetInputStream : public InputStreamBase {
private:
    hailo_stream_eth_input_configuration_t configuration;
    Udp m_udp;
    bool m_is_stream_activated;
    Device &m_device;

    hailo_status eth_stream__config_input_sync_params(uint32_t frames_per_sync);
    hailo_status eth_stream__write_all_no_sync(const void *buffer, size_t offset, size_t size);
    hailo_status eth_stream__write_all_with_sync(const void *buffer, size_t offset, size_t size);
    hailo_status set_timeout(std::chrono::milliseconds timeout);
    void set_max_payload_size(uint16_t size);

protected:
    virtual hailo_status eth_stream__write_with_remainder(const void *buffer, size_t offset, size_t size, size_t remainder_size);
    Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer);
    virtual hailo_status write_impl(const MemoryView &buffer) override;

public:
    EthernetInputStream(Device &device, Udp &&udp, EventPtr &&core_op_activated_event, const LayerInfo &layer_info, hailo_status &status) :
        InputStreamBase(layer_info, std::move(core_op_activated_event), status), m_udp(std::move(udp)), m_device(device) {}
    virtual ~EthernetInputStream();

    static Expected<std::unique_ptr<EthernetInputStream>> create(Device &device,
        const LayerInfo &edge_layer, const hailo_eth_input_stream_params_t &params, EventPtr core_op_activated_event);

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override
    {
        CHECK(buffer_mode == StreamBufferMode::OWNING, HAILO_INVALID_ARGUMENT,
            "Ethernet streams supports only sync api");
        return HAILO_SUCCESS;
    }

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_ETH; }
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override {return HAILO_SUCCESS;}; // TODO (HRT-3799): clear abort state in the eth stream
};

class EthernetInputStreamRateLimited : public EthernetInputStream {
protected:
    const uint32_t rate_bytes_per_sec;

public:
    EthernetInputStreamRateLimited(Device &device, Udp &&udp, EventPtr &&core_op_activated_event,
        uint32_t rate_bytes_per_sec, const LayerInfo &layer_info, hailo_status &status);
    virtual ~EthernetInputStreamRateLimited() = default;
};

class TokenBucketEthernetInputStream : public EthernetInputStreamRateLimited {
private:
    DynamicTokenBucket token_bucket;
    // Note:
    // * We set the token bucket's burst size to be our MTU. If we'd use larger burst sizes
    //   we could send packets faster than the desired rate.
    // * We send packets with at most MAX_UDP_PAYLOAD_SIZE bytes of data. Hence we won't
    //   consume more than MAX_UDP_PAYLOAD_SIZE tokens from the token bucket.
    static const uint32_t BURST_SIZE = MAX_UDP_PAYLOAD_SIZE;
    static const uint32_t MAX_CONSUME_SIZE = MAX_UDP_PAYLOAD_SIZE;

protected:
    virtual hailo_status eth_stream__write_with_remainder(const void *buffer, size_t offset, size_t size, size_t remainder_size) override;

public:
    TokenBucketEthernetInputStream(Device &device, Udp &&udp, EventPtr &&core_op_activated_event,
        uint32_t rate_bytes_per_sec, const LayerInfo &layer_info, hailo_status &status);
    virtual ~TokenBucketEthernetInputStream() = default;
};


#if defined(__GNUC__)
class TrafficControlEthernetInputStream : public EthernetInputStreamRateLimited {
public:
    static Expected<std::unique_ptr<TrafficControlEthernetInputStream>> create(Device &device, Udp &&udp,
        EventPtr &&core_op_activated_event, uint32_t rate_bytes_per_sec, const LayerInfo &layer_info);
    virtual ~TrafficControlEthernetInputStream() = default;

private:
    TrafficControlEthernetInputStream(Device &device, Udp &&udp, EventPtr &&core_op_activated_event,
        uint32_t rate_bytes_per_sec, TrafficControl &&tc, const LayerInfo &layer_info, hailo_status &status);
    static Expected<std::string> get_interface_address(const struct in_addr *addr);

    TrafficControl m_tc;
};
#endif

class EthernetOutputStream : public OutputStreamBase {
private:
    uint8_t leftover_buffer[MAX_UDP_PAYLOAD_SIZE];
    size_t leftover_size = 0;
    uint32_t last_seen_sync_index;
    bool encountered_timeout;
    hailo_stream_eth_output_configuration_t configuration;
    Udp m_udp;
    bool m_is_stream_activated;
    Device &m_device;

    EthernetOutputStream(Device &device, const LayerInfo &edge_layer, Udp &&udp, EventPtr &&core_op_activated_event, hailo_status &status) :
        OutputStreamBase(edge_layer, std::move(core_op_activated_event), status),
        leftover_buffer(),
        leftover_size(0),
        // Firmware starts sending sync sequence from 0, so treating the first previous as max value (that will be overflowed to 0)
        last_seen_sync_index(std::numeric_limits<uint32_t>::max()),
        encountered_timeout(false),
        configuration(),
        m_udp(std::move(udp)),
        m_device(device)
    {}

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override
    {
        CHECK(buffer_mode == StreamBufferMode::OWNING, HAILO_INVALID_ARGUMENT,
            "Ethernet streams supports only sync api");
        return HAILO_SUCCESS;
    }

    hailo_status read_impl(MemoryView buffer) override;
    hailo_status read_all_with_sync(void *buffer, size_t offset, size_t size);
    hailo_status read_all_no_sync(void *buffer, size_t offset, size_t size);

    static bool is_sync_packet(const void* buffer, size_t offset, size_t transfer_size);
    static bool is_sync_expected(size_t offset, size_t initial_offset, size_t frame_size);
    hailo_status handle_timeout(const void* buffer, size_t offset, size_t initial_offset, size_t frame_size);
    hailo_status set_timeout(std::chrono::milliseconds timeout);
    hailo_status get_last_sync();

    static hailo_status fill_output_stream_ptr_with_info(const hailo_eth_output_stream_params_t &params, EthernetOutputStream *stream);

public:
    virtual ~EthernetOutputStream();

    Expected<size_t> sync_read_raw_buffer(MemoryView &buffer);

    static Expected<std::unique_ptr<EthernetOutputStream>> create(Device &device, const LayerInfo &edge_layer,
        const hailo_eth_output_stream_params_t &params, EventPtr core_op_activated_event);

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_ETH; }
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort_impl() override;
    virtual hailo_status clear_abort_impl() override {return HAILO_SUCCESS;}; // TODO (HRT-3799): clear abort state in the eth stream
};

} /* namespace hailort */

#endif /* HAILO_ETH_STREAM_H_ */
