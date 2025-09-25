/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file eth_stream.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/



#include "hailo/hailort.h"
#include "hailo/stream.hpp"
#include "hailo/hef.hpp"
#include "hailo/hailort_common.hpp"

#include "common/ethernet_utils.hpp"
#include "common/utils.hpp"

#include "eth/eth_stream.hpp"
#include "eth/eth_device.hpp"
#include "eth/token_bucket.hpp"
#include "device_common/control.hpp"

#include <new>
#include <stdlib.h>
#include <math.h>
#include <byte_order.h>


namespace hailort
{

#define SYNC_PACKET_BARKER (0xa143341a)


typedef struct hailo_output_sync_packet_t {
    uint32_t barker;
    uint32_t sequence_index;
} hailo_output_sync_packet_t;

EthernetInputStream::~EthernetInputStream()
{
    if (m_is_stream_activated) {
        auto status = this->deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Close stream failed! (status {} stream index {})", status, m_stream_info.index);
        }
    }
}


Expected<Udp> eth_stream__create_udp(EthernetDevice *eth_device, struct sockaddr_in host_address, uint8_t stream_index,
    port_t device_port, bool is_input)
{
    if (HAILO_DEFAULT_ETH_DEVICE_PORT == device_port) {
        if (is_input) {
            device_port = (uint16_t)(stream_index + HailoRTCommon::ETH_INPUT_BASE_PORT);
        } else {
            device_port = (uint16_t)(stream_index + HailoRTCommon::ETH_OUTPUT_BASE_PORT);
        }
    }

    return Udp::create(eth_device->get_device_info().device_address.sin_addr, device_port, host_address.sin_addr,
        host_address.sin_port);
}

/** Input stream **/
hailo_status EthernetInputStream::deactivate_stream()
{
    hailo_status status = HAILO_UNINITIALIZED;

    if (!m_is_stream_activated) {
        return HAILO_SUCCESS;
    }

    m_is_stream_activated = false;

    status = Control::close_stream(m_device, m_dataflow_manager_id, true);
    CHECK_SUCCESS(status);

    // Aborting the stream to make sure all read/writes will exit.
    // Note - on ethernet stream there is no true "clear_abort" - one abort was called, the socket can't be reused.
    status = abort_impl();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status EthernetInputStream::activate_stream()
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__config_stream_params_t params = {};
    
    params.nn_stream_config = m_layer_info.nn_stream_config;
    params.communication_type = CONTROL_PROTOCOL__COMMUNICATION_TYPE_UDP;
    params.is_input = true;
    params.stream_index = m_stream_info.index;
    params.communication_params.udp_input.listening_port = (uint16_t)(BYTE_ORDER__htons(m_udp.m_device_address.sin_port));
    params.skip_nn_stream_config = false;
    // Currently hardcoded assign as there are no power mode optimizations over eth
    params.power_mode = static_cast<uint8_t>(CONTROL_PROTOCOL__MODE_ULTRA_PERFORMANCE);

    if (this->configuration.is_sync_enabled) {
        params.communication_params.udp_input.sync.should_sync = true;
        params.communication_params.udp_input.sync.frames_per_sync = this->configuration.frames_per_sync;
        params.communication_params.udp_input.sync.packets_per_frame = this->configuration.packets_per_frame;
        params.communication_params.udp_input.sync.sync_size = this->configuration.sync_size;
    }

    params.communication_params.udp_input.buffers_threshold = this->configuration.buffers_threshold;
    params.communication_params.udp_input.use_rtp = false;

    status = Control::config_stream_udp_input(m_device, &params, m_dataflow_manager_id);
    CHECK_SUCCESS(status);

    status = Control::open_stream(m_device, m_dataflow_manager_id, true);
    CHECK_SUCCESS(status);

    m_is_stream_activated = true;

    return HAILO_SUCCESS;
}

Expected<size_t> EthernetInputStream::sync_write_raw_buffer(const MemoryView &buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = get_core_op_activated_event()->wait(std::chrono::milliseconds(0));
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != status, HAILO_NETWORK_GROUP_NOT_ACTIVATED, "Trying to write on stream before its network_group is activated");
    CHECK_SUCCESS_AS_EXPECTED(status);

    size_t size = buffer.size();
    status = m_udp.send((uint8_t*)buffer.data(), &size, this->configuration.use_dataflow_padding, this->configuration.max_payload_size);
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Udp send was aborted!");
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status, "{} (H2D) failed with status={}", name(), status);

    return size;
}

hailo_status EthernetInputStream::write_impl(const MemoryView &buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    CHECK(buffer.size() >= MIN_UDP_PAYLOAD_SIZE, HAILO_INVALID_ARGUMENT, "Input must be larger than {}", MIN_UDP_PAYLOAD_SIZE);
    CHECK(((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) == 0), HAILO_INVALID_ARGUMENT,
        "Input must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());

    const size_t offset = 0;
    if (this->configuration.is_sync_enabled) {
        status = eth_stream__write_all_with_sync(buffer.data(), offset, buffer.size());
    } else {
        status = eth_stream__write_all_no_sync(buffer.data(), offset, buffer.size());
    }
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("eth_stream__write_all was aborted!");
        return status;
    }
    
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status EthernetInputStream::eth_stream__write_all_no_sync(const void *buffer, size_t offset, size_t size) {
    size_t remainder_size = 0;
    size_t packet_size = this->configuration.max_payload_size;

    //if we have padding, consider it when calculating the packet sizes
    if (this->configuration.use_dataflow_padding) {
        packet_size -= PADDING_BYTES_SIZE + PADDING_ALIGN_BYTES;
    }

    remainder_size = size % packet_size;

    if ((0 < remainder_size) && (remainder_size < MIN_UDP_PAYLOAD_SIZE)) {
        remainder_size = MIN_UDP_PAYLOAD_SIZE;
    }
    return eth_stream__write_with_remainder(buffer, offset, size, remainder_size);
}

hailo_status EthernetInputStream::eth_stream__write_with_remainder(const void *buffer, size_t offset, size_t size, size_t remainder_size) {
    size_t transfer_size = 0;
    size_t offset_end_without_remainder = offset + size - remainder_size;

    while (offset < offset_end_without_remainder) {
        transfer_size = offset_end_without_remainder - offset;
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, const auto bytes_written,
            sync_write_raw_buffer(MemoryView::create_const(static_cast<const uint8_t*>(buffer) + offset, transfer_size)));
        offset += bytes_written;
    }
    if (0 < remainder_size) {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, const auto bytes_written,
            sync_write_raw_buffer(MemoryView::create_const(static_cast<const uint8_t*>(buffer) + offset, remainder_size)));
        (void)bytes_written;
        assert(bytes_written == remainder_size);
    }

    return HAILO_SUCCESS;
}

EthernetInputStreamRateLimited::EthernetInputStreamRateLimited(Device &device, Udp &&udp,
    EventPtr &&core_op_activated_event, uint32_t rate_bytes_per_sec, const LayerInfo &layer_info, hailo_status &status) :
    EthernetInputStream::EthernetInputStream(device, std::move(udp), std::move(core_op_activated_event), layer_info, status),
    rate_bytes_per_sec(rate_bytes_per_sec)
{}

TokenBucketEthernetInputStream::TokenBucketEthernetInputStream(Device &device, Udp &&udp,
    EventPtr &&core_op_activated_event, uint32_t rate_bytes_per_sec, const LayerInfo &layer_info, hailo_status &status) :
    EthernetInputStreamRateLimited::EthernetInputStreamRateLimited(device, std::move(udp),
        std::move(core_op_activated_event), rate_bytes_per_sec, layer_info, status),
    token_bucket()
{}

hailo_status TokenBucketEthernetInputStream::eth_stream__write_with_remainder(const void *buffer, size_t offset, size_t size, size_t remainder_size) {
    size_t transfer_size = 0;
    size_t offset_end_without_remainder = offset + size - remainder_size;

    assert(remainder_size <= MAX_CONSUME_SIZE);
    static_assert(MAX_CONSUME_SIZE <= BURST_SIZE, "We are asking to consume more bytes than the size of the token bucket, this will fail");

    while (offset < offset_end_without_remainder) {
        (void)token_bucket.consumeWithBorrowAndWait(MAX_CONSUME_SIZE, rate_bytes_per_sec, BURST_SIZE);
    
        transfer_size = offset_end_without_remainder - offset;
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, const auto bytes_written,
            sync_write_raw_buffer(MemoryView::create_const(static_cast<const uint8_t*>(buffer) + offset, transfer_size)));
        offset += bytes_written;
    }
    if (0 < remainder_size) {
        // We don't static_assert that "remainder_size <= BURST_SIZE", so the call could fail in theory.
        // However, since remainder_size is modulo MAX_UDP_PAYLOAD_SIZE and BURST_SIZE == MAX_UDP_PAYLOAD_SIZE, it should be smaller.
        (void)token_bucket.consumeWithBorrowAndWait(static_cast<double>(remainder_size), rate_bytes_per_sec, BURST_SIZE);

        TRY_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, const auto bytes_written,
            sync_write_raw_buffer(MemoryView::create_const(static_cast<const uint8_t*>(buffer) + offset, remainder_size)));
        (void)bytes_written;
        assert(bytes_written == remainder_size);
    }

    return HAILO_SUCCESS;
}

#if defined(__GNUC__)
Expected<std::unique_ptr<TrafficControlEthernetInputStream>> TrafficControlEthernetInputStream::create(
    Device &device, Udp &&udp, EventPtr &&core_op_activated_event, uint32_t rate_bytes_per_sec, const LayerInfo &layer_info)
{
    TRY(const auto board_ip, get_interface_address(&udp.m_device_address.sin_addr));
    const auto board_port = BYTE_ORDER__ntohs(udp.m_device_address.sin_port);

    TRY(auto tc, TrafficControl::create(board_ip, board_port, rate_bytes_per_sec),
        "Creating traffic control at rate {} failed", rate_bytes_per_sec);

    auto status = HAILO_UNINITIALIZED;
    // Note: we don't use make_unique because TrafficControlEthernetInputStream's ctor is private
    auto tc_ptr = std::unique_ptr<TrafficControlEthernetInputStream>(new (std::nothrow)
        TrafficControlEthernetInputStream(device, std::move(udp), std::move(core_op_activated_event), rate_bytes_per_sec,
        std::move(tc), layer_info, status));
    CHECK_AS_EXPECTED(nullptr != tc_ptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return tc_ptr;
}

Expected<std::string> TrafficControlEthernetInputStream::get_interface_address(const struct in_addr *addr)
{
    TRY(const auto ip, Buffer::create(IPV4_STRING_MAX_LENGTH, 0));

    const auto result = Socket::ntop(AF_INET, addr, ip.as_pointer<char>(), EthernetUtils::MAX_INTERFACE_SIZE);
    CHECK_SUCCESS_AS_EXPECTED(result, "Failed parsing IP to string with status {}", result);
    
    return ip.to_string();
}

TrafficControlEthernetInputStream::TrafficControlEthernetInputStream(Device &device, Udp &&udp,
    EventPtr &&core_op_activated_event, uint32_t rate_bytes_per_sec, TrafficControl &&tc, const LayerInfo &layer_info, hailo_status &status) :
    EthernetInputStreamRateLimited(device, std::move(udp), std::move(core_op_activated_event), rate_bytes_per_sec, layer_info, status),
    m_tc(std::move(tc))
{}
#endif

hailo_status EthernetInputStream::eth_stream__write_all_with_sync(const void *buffer, size_t offset, size_t size) {
    hailo_status status = HAILO_UNINITIALIZED;
    size_t number_of_frames = 0;
    size_t frame_size = m_stream_info.hw_frame_size;

    if (0 != (size % frame_size)) {
        LOGGER__ERROR("Read size is not a multiple of frame size."
                      "This operation is not possible with the sync packet mode."
                      "Tried to read {} bytes and frame size is {}", size, m_stream_info.hw_frame_size);
        return HAILO_INVALID_ARGUMENT;
    }

    number_of_frames = size / frame_size;
    for (size_t i = 0; i < number_of_frames; i++) {
        // Write frame by frame, whereas the remainder packet is the sync packet
        status = eth_stream__write_with_remainder(buffer, offset, frame_size, this->configuration.sync_size);
        if (HAILO_STREAM_ABORT == status) {
            LOGGER__INFO("eth_stream__write_with_remainder was aborted!");
            return status;
        }
        CHECK_SUCCESS(status);
        offset += frame_size;
    }

    return HAILO_SUCCESS;
}

hailo_status EthernetInputStream::eth_stream__config_input_sync_params(uint32_t frames_per_sync)
{
    size_t packet_size = MAX_UDP_PAYLOAD_SIZE;

    if (MAX_UDP_PAYLOAD_SIZE >= m_stream_info.hw_frame_size) {
        LOGGER__WARNING("Input size that isn't larger than {} doesn't benefit from sync, disabling..", MAX_UDP_PAYLOAD_SIZE);
        this->configuration.is_sync_enabled = false;
        return HAILO_SUCCESS;
    }
    this->configuration.is_sync_enabled = true;
    CHECK(1 == frames_per_sync, HAILO_NOT_IMPLEMENTED,
        "Currently not supported frames_per_sync != 1");
    this->configuration.frames_per_sync = frames_per_sync;
    //if we have padding, consider it when determining the number of packets
    if (this->configuration.use_dataflow_padding) {
        packet_size = MAX_UDP_PADDED_PAYLOAD_SIZE;
    }
    // Data packets per frame are all of the packets except the sync
    this->configuration.packets_per_frame = (uint32_t) ceil((double) m_stream_info.hw_frame_size / (double) packet_size) - 1;
    if (0 == (m_stream_info.hw_frame_size % packet_size)) {
        // If there is no remainder to make the sync packet, we will "cut" it from the last data packet, thus increasing the number of packets.
        this->configuration.packets_per_frame++;
    }
    // Make the remainder packet the sync packet
    this->configuration.sync_size = (uint16_t)(m_stream_info.hw_frame_size % packet_size);

    if (MIN_UDP_PAYLOAD_SIZE > this->configuration.sync_size) {
        // If the remainder isn't big enough, we'll "cut" from the last data packet enough to fill the minimum size.
        this->configuration.sync_size = MIN_UDP_PAYLOAD_SIZE;
    }
    LOGGER__DEBUG("Configured sync size {}, packets per frame {}", this->configuration.sync_size, this->configuration.packets_per_frame);
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<EthernetInputStream>> EthernetInputStream::create(Device &device,
    const LayerInfo &edge_layer, const hailo_eth_input_stream_params_t &params, EventPtr core_op_activated_event)
{
    hailo_status status = HAILO_UNINITIALIZED;
    // TODO: try to avoid cast
    auto eth_device = reinterpret_cast<EthernetDevice*>(&device);
    std::unique_ptr<EthernetInputStream> local_stream;

    auto stream_index = edge_layer.stream_index;
    TRY(auto udp, eth_stream__create_udp(eth_device, params.host_address, stream_index, params.device_port, true));

    if (params.rate_limit_bytes_per_sec == 0) {
        local_stream = std::unique_ptr<EthernetInputStream>(
            new (std::nothrow) EthernetInputStream(device, std::move(udp), std::move(core_op_activated_event), edge_layer, status));
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else {
#ifdef _MSC_VER
        // TODO: Add factory class
        local_stream = std::unique_ptr<EthernetInputStream>(
            new (std::nothrow) TokenBucketEthernetInputStream(device, std::move(udp),
            std::move(core_op_activated_event), params.rate_limit_bytes_per_sec, edge_layer, status));
        CHECK_SUCCESS_AS_EXPECTED(status);
#else
        TRY(local_stream, TrafficControlEthernetInputStream::create(device, std::move(udp),
            std::move(core_op_activated_event), params.rate_limit_bytes_per_sec, edge_layer));
#endif
    }

    CHECK_AS_EXPECTED((nullptr != local_stream), HAILO_OUT_OF_HOST_MEMORY);
    local_stream->m_is_stream_activated = false;

    TRY(const auto device_architecture, eth_device->get_architecture());
    if ((HAILO_ARCH_HAILO8 == device_architecture) || (HAILO_ARCH_HAILO8L == device_architecture)) {
        local_stream->configuration.use_dataflow_padding = true;
    }
    else {
        local_stream->configuration.use_dataflow_padding = false;
    }

    local_stream->set_max_payload_size(params.max_payload_size);

    local_stream->configuration.is_sync_enabled = params.is_sync_enabled;
    if (local_stream->configuration.is_sync_enabled) {
        status = local_stream->eth_stream__config_input_sync_params(params.frames_per_sync);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    local_stream->configuration.buffers_threshold = params.buffers_threshold;

    return local_stream;
}

void EthernetInputStream::set_max_payload_size(uint16_t size)
{
    if (size > MAX_UDP_PAYLOAD_SIZE) {
        size = MAX_UDP_PAYLOAD_SIZE;
    }
    this->configuration.max_payload_size = size;
}

hailo_status EthernetInputStream::set_timeout(std::chrono::milliseconds timeout)
{
    return m_udp.set_timeout(timeout);
}

std::chrono::milliseconds EthernetInputStream::get_timeout() const
{
    return std::chrono::milliseconds((MILLISECONDS_IN_SECOND * m_udp.m_timeout.tv_sec) + (m_udp.m_timeout.tv_usec / MICROSECONDS_IN_MILLISECOND));
}

hailo_status EthernetInputStream::abort_impl()
{
    return m_udp.abort();
}

/** Output stream **/
EthernetOutputStream::~EthernetOutputStream()
{
    if (m_is_stream_activated) {
        auto status = this->deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Close stream failed! (status {} stream index {})", status, m_stream_info.index);
        }
    }
}

hailo_status EthernetOutputStream::deactivate_stream()
{
    hailo_status status = HAILO_UNINITIALIZED;

    if (!m_is_stream_activated) {
        return HAILO_SUCCESS;
    }

    m_is_stream_activated = false;

    status = Control::close_stream(m_device, m_dataflow_manager_id, false);
    CHECK_SUCCESS(status);

    // Aborting the stream to make sure all read/writes will exit.
    // Note - on ethernet stream there is no true "clear_abort" - one abort was called, the socket can't be reused.
    status = abort_impl();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status EthernetOutputStream::activate_stream()
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__config_stream_params_t params = {};

    params.nn_stream_config = m_layer_info.nn_stream_config;
    params.communication_type = CONTROL_PROTOCOL__COMMUNICATION_TYPE_UDP;
    params.is_input = false;
    params.stream_index = m_stream_info.index;
    params.skip_nn_stream_config = false;
    // Currently hardcoded assign as there are no power mode optimizations over eth
    params.power_mode = static_cast<uint8_t>(CONTROL_PROTOCOL__MODE_ULTRA_PERFORMANCE);

    params.communication_params.udp_output.chip_udp_port = (uint16_t)(BYTE_ORDER__htons(m_udp.m_device_address.sin_port));
    params.communication_params.udp_output.host_udp_port = (uint16_t)(BYTE_ORDER__htons(m_udp.m_host_address.sin_port));
    params.communication_params.udp_output.max_udp_payload_size = this->configuration.max_payload_size;
    params.communication_params.udp_output.buffers_threshold = this->configuration.buffers_threshold;
    params.communication_params.udp_output.use_rtp = false;

    if (this->configuration.is_sync_enabled) {
        params.communication_params.udp_output.should_send_sync_packets = true;
    }

    status = Control::config_stream_udp_output(m_device, &params, m_dataflow_manager_id);
    CHECK_SUCCESS(status);

    status = Control::open_stream(m_device, m_dataflow_manager_id, false);
    CHECK_SUCCESS(status);

    m_is_stream_activated = true;

    return HAILO_SUCCESS;
}

hailo_status EthernetOutputStream::read_all_no_sync(void *buffer, size_t offset, size_t size) {
    size_t offset_end = 0;
    size_t transfer_size = 0;

    offset_end = offset + size;
    while (offset < offset_end) {
        transfer_size = offset_end - offset;
        MemoryView buffer_view(static_cast<uint8_t*>(buffer) + offset, transfer_size);
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_STREAM_ABORT, auto bytes_read, this->sync_read_raw_buffer(buffer_view));
        offset += bytes_read;
    }

    return HAILO_SUCCESS;
}

hailo_status EthernetOutputStream::read_all_with_sync(void *buffer, size_t offset, size_t size) {
    hailo_status status = HAILO_UNINITIALIZED;
    size_t initial_offset = offset;
    size_t offset_end = offset + size;
    bool got_last_sync_early = false;
    const size_t frame_size = m_stream_info.hw_frame_size;
    bool is_batch_invalid = false;

    if ((size % frame_size) != 0) {
        LOGGER__ERROR("Read size is not a multiple of frame size."
                      "This operation is not possible with the sync packet mode."
                      "Tried to read {} bytes and frame size is {}", size, frame_size);
        return HAILO_INVALID_ARGUMENT;
    }

    if (this->leftover_size > 0) {
        memcpy((uint8_t*)buffer + offset, this->leftover_buffer, this->leftover_size);
        offset += this->leftover_size;
        // leftover size will be reassigned in the end, but in case the function ends prematurely we will zero it for safety.
        this->leftover_size = 0;
    }

    while (offset < offset_end) {
        size_t transfer_size = offset_end - offset;
        MemoryView buffer_view(static_cast<uint8_t*>(buffer) + offset, transfer_size);
        auto expected_bytes_read = this->sync_read_raw_buffer(buffer_view);
        status = expected_bytes_read.status();
        if (HAILO_TIMEOUT == status) {
            return handle_timeout(buffer, offset, initial_offset, frame_size);
        } else if (HAILO_STREAM_ABORT == status) {
            LOGGER__INFO("sync_read_raw_buffer was aborted");
            return status;
        } else if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("read failed");
            return status;
        }
        transfer_size = expected_bytes_read.release();
        if (is_sync_packet(buffer, offset, transfer_size)) {
            uint32_t sequence_index = BYTE_ORDER__ntohl(((hailo_output_sync_packet_t*)((uint8_t*)buffer + offset))->sequence_index);
            if (is_sync_expected(offset, initial_offset, frame_size)) {
                if (sequence_index != (this->last_seen_sync_index + 1)) {
                    // Batch is invalid if a frame was skipped
                    is_batch_invalid = true;
                    LOGGER__WARNING("Received {} frames. Missed sync packets between them, treating the batch as invalid data", sequence_index - this->last_seen_sync_index);
                }
                if (sequence_index == this->last_seen_sync_index) {
                    LOGGER__ERROR("Got duplicate sync!");
                    return HAILO_INTERNAL_FAILURE;
                }
            } else {
                size_t number_of_missing_bytes = (frame_size - ((offset - initial_offset) % frame_size));
                LOGGER__WARNING("Some bytes are missing at frame, padding {} bytes with zeros", number_of_missing_bytes);
                memset((uint8_t*)buffer + offset, 0, number_of_missing_bytes);
                offset += number_of_missing_bytes;
                if (offset == offset_end) {
                    got_last_sync_early = true;
                }
                is_batch_invalid = true;
            }
            this->last_seen_sync_index = sequence_index;
        } else {
            offset += transfer_size;
        }
    }

    status = HAILO_SUCCESS;

    if (!got_last_sync_early) {
        status = get_last_sync();
    }
    if (HAILO_SUCCESS == status && is_batch_invalid) {
        return HAILO_INVALID_FRAME;
    }

    return HAILO_SUCCESS;
}

hailo_status EthernetOutputStream::get_last_sync() {
    size_t last_packet_size = sizeof(this->leftover_buffer);
    MemoryView leftover_buffer_view(this->leftover_buffer, last_packet_size);
    auto expected_bytes_read = sync_read_raw_buffer(leftover_buffer_view);
    CHECK(HAILO_TIMEOUT != expected_bytes_read.status(), HAILO_INVALID_FRAME, "Got timeout on last sync, marking last frame as invalid");
    CHECK_EXPECTED_AS_STATUS(expected_bytes_read, "Recv error"); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
    last_packet_size = expected_bytes_read.release();

    if (is_sync_packet(this->leftover_buffer, 0, last_packet_size)) {
        this->leftover_size = 0;
    } else {
        LOGGER__WARNING("Received a data packet instead of sync, saving leftover for later frame");
        this->leftover_size = last_packet_size;
    }

    return HAILO_SUCCESS;
}

hailo_status EthernetOutputStream::handle_timeout(const void* buffer, size_t offset,
                                                       size_t initial_offset, const size_t frame_size) {
    // In case data a timeout has occurred, and data was received, try filling missing in frame
    if (this->encountered_timeout || (offset == initial_offset)) {
        LOGGER__ERROR("{} (D2H) got timeout (timeout={}ms), unable to complete the frame", name(), get_timeout().count());
        return HAILO_TIMEOUT;
    }
    LOGGER__ERROR("Received timeout. Continuing logic as if a sync packet was received");
    size_t number_of_missing_bytes = (frame_size - ((offset - initial_offset) % frame_size));
    LOGGER__ERROR("padding {} bytes with zeros because of timeout", number_of_missing_bytes);
    memset((uint8_t*)buffer + offset, 0, number_of_missing_bytes);
    this->encountered_timeout = true;
    return HAILO_INVALID_FRAME;
}

bool EthernetOutputStream::is_sync_expected(size_t offset, size_t initial_offset, const size_t frame_size) {
    return (((offset - initial_offset) % frame_size) == 0) && (offset > initial_offset);
}

bool EthernetOutputStream::is_sync_packet(const void* buffer, size_t offset, size_t transfer_size) {
    return (transfer_size == sizeof(hailo_output_sync_packet_t) &&
            ((hailo_output_sync_packet_t*)((uint8_t*)buffer + offset))->barker == BYTE_ORDER__ntohl(SYNC_PACKET_BARKER));
}

hailo_status EthernetOutputStream::read_impl(MemoryView buffer)
{
    if ((buffer.size() % HailoRTCommon::HW_DATA_ALIGNMENT) != 0) {
        LOGGER__ERROR("Size must be aligned to {} (got {})", HailoRTCommon::HW_DATA_ALIGNMENT, buffer.size());
        return HAILO_INVALID_ARGUMENT;
    }

    hailo_status status = HAILO_UNINITIALIZED;
    if (this->configuration.is_sync_enabled) {
        status = this->read_all_with_sync(buffer.data(), 0, buffer.size());
    } else {
        status = this->read_all_no_sync(buffer.data(), 0, buffer.size());
    }
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("read was aborted!");
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<size_t> EthernetOutputStream::sync_read_raw_buffer(MemoryView &buffer)
{
    auto status = get_core_op_activated_event()->wait(std::chrono::milliseconds(0));
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != status, HAILO_NETWORK_GROUP_NOT_ACTIVATED, 
        "Trying to read on stream before its network_group is activated");
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto buffer_size = buffer.size();
    status = m_udp.recv((uint8_t*)buffer.data(),&buffer_size);
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Udp recv was aborted!");
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status, "{} (D2H) failed with status={}", name(), status);

    return buffer_size;
}

hailo_status EthernetOutputStream::fill_output_stream_ptr_with_info(const hailo_eth_output_stream_params_t &params, EthernetOutputStream *stream)
{
    if ((HailoRTCommon::is_nms(stream->m_stream_info)) && (params.is_sync_enabled)) {
        LOGGER__WARNING("NMS is not supported with sync enabled. Setting sync flag to false");
        stream->configuration.is_sync_enabled = false;
    } else {
        stream->configuration.is_sync_enabled = params.is_sync_enabled;
    }

    stream->configuration.max_payload_size = params.max_payload_size;
    stream->configuration.buffers_threshold = params.buffers_threshold;

    stream->m_is_stream_activated = false;
    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<EthernetOutputStream>> EthernetOutputStream::create(Device &device,
    const LayerInfo &edge_layer, const hailo_eth_output_stream_params_t &params, EventPtr core_op_activated_event)
{
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<EthernetOutputStream> local_stream = nullptr;
    // TODO: try to avoid cast
    auto eth_device = reinterpret_cast<EthernetDevice*>(&device);

    const auto stream_index = edge_layer.stream_index;
    TRY(auto udp, eth_stream__create_udp(eth_device, params.host_address, stream_index, params.device_port, false));
    local_stream = std::unique_ptr<EthernetOutputStream>(new (std::nothrow) EthernetOutputStream(device,
        edge_layer, std::move(udp), std::move(core_op_activated_event), status));
    CHECK((nullptr != local_stream), make_unexpected(HAILO_OUT_OF_HOST_MEMORY));
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = fill_output_stream_ptr_with_info(params, local_stream.get());
    CHECK_SUCCESS_AS_EXPECTED(status);
    return local_stream;
}

hailo_status EthernetOutputStream::set_timeout(std::chrono::milliseconds timeout)
{
    return m_udp.set_timeout(timeout);
}

std::chrono::milliseconds EthernetOutputStream::get_timeout() const
{
    return std::chrono::milliseconds((MILLISECONDS_IN_SECOND * m_udp.m_timeout.tv_sec) + (m_udp.m_timeout.tv_usec / MICROSECONDS_IN_MILLISECOND));
}

hailo_status EthernetOutputStream::abort_impl()
{
    return m_udp.abort();
}

} /* namespace hailort */
