/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_driver.hpp
 * @brief Low level interface to PCI driver
 *
 * 
 **/
#ifndef _HAILORT_DRIVER_HPP_
#define _HAILORT_DRIVER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "d2h_event_queue.hpp"
#include "os/file_descriptor.hpp"

#include <mutex>
#include <thread>
#include <chrono>
#include <utility>

namespace hailort
{

#define DEVICE_NODE_NAME       "hailo"

#define PENDING_BUFFERS_SIZE (128)
static_assert((0 == ((PENDING_BUFFERS_SIZE - 1) &  PENDING_BUFFERS_SIZE)), "PENDING_BUFFERS_SIZE must be a power of 2");

#define MIN_ACTIVE_TRANSFERS_SCALE (2)
#define MAX_ACTIVE_TRANSFERS_SCALE (4)

#define HAILO_MAX_BATCH_SIZE ((PENDING_BUFFERS_SIZE / MIN_ACTIVE_TRANSFERS_SCALE) - 1)

// When measuring latency, each channel is capable of PENDING_BUFFERS_SIZE active transfers, each transfer raises max of 2 timestamps
#define MAX_IRQ_TIMESTAMPS_SIZE (PENDING_BUFFERS_SIZE * 2)

#define DESCRIPTORS_IN_BUFFER(buffer_size, desc_page_size) (((buffer_size) + (desc_page_size) - 1) / (desc_page_size))

#define PCIE_EXPECTED_MD5_LENGTH (16)

enum class PciBar {
    bar0 = 0,
    bar1,
    bar2,
    bar3,
    bar4,
    bar5,
};

// NOTE: don't change members from this struct without updating all code using it (platform specific)
struct ChannelInterruptTimestamp {
    std::chrono::nanoseconds timestamp;
    uint16_t desc_num_processed;
};

struct ChannelInterruptTimestampList {
    ChannelInterruptTimestamp timestamp_list[MAX_IRQ_TIMESTAMPS_SIZE];
    size_t count;
};

class HailoRTDriver final
{
public:

    struct DeviceInfo {
        std::string dev_path;

        // Board information
        uint32_t vendor_id;
        uint32_t device_id;

        // PCIe board location
        uint32_t domain;
        uint32_t bus;
        uint32_t device;
        uint32_t func;
    };

    enum class DmaDirection {
        H2D = 0,
        D2H,
        BOTH
    };

    // TODO: move to general place
    enum class BoardType {
        HAILO8 = 0,
        MERCURY = 1
    };

    enum class DmaType {
        PCIE,
        DRAM
    };

    using VdmaBufferHandle = size_t;
    using VdmaChannelHandle = uint64_t;

    static Expected<HailoRTDriver> create(const std::string &dev_path);

    static Expected<std::vector<DeviceInfo>> scan_pci();

    hailo_status read_bar(PciBar bar, off_t offset, size_t size, void *buf);
    hailo_status write_bar(PciBar bar, off_t offset, size_t size, const void *buf);

    Expected<uint32_t> read_vdma_channel_registers(off_t offset, size_t size);
    hailo_status write_vdma_channel_registers(off_t offset, size_t size, uint32_t data);

    hailo_status vdma_buffer_sync(VdmaBufferHandle buffer, DmaDirection sync_direction, void *address, size_t buffer_size);

    Expected<VdmaChannelHandle> vdma_channel_enable(uint32_t channel_index, DmaDirection data_direction,
        uintptr_t desc_list_handle, bool enable_timestamps_measure);
    hailo_status vdma_channel_disable(uint32_t channel_index, VdmaChannelHandle channel_handle);
    Expected<ChannelInterruptTimestampList> wait_channel_interrupts(uint32_t channel_index, VdmaChannelHandle channel_handle,
        const std::chrono::milliseconds &timeout);
    hailo_status vdma_channel_abort(uint32_t channel_index, VdmaChannelHandle channel_handle);
    hailo_status vdma_channel_clear_abort(uint32_t channel_index, VdmaChannelHandle channel_handle);

    Expected<D2H_EVENT_MESSAGE_t> read_notification();
    hailo_status disable_notifications();

    hailo_status fw_control(const void *request, size_t request_len, const uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH],
        void *response, size_t *response_len, uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH],
        std::chrono::milliseconds timeout, hailo_cpu_id_t cpu_id);

    /**
     * Read data from the debug log buffer.
     *
     * @param[in]     buffer            - A pointer to the buffer that would receive the debug log data.
     * @param[in]     buffer_size       - The size in bytes of the buffer.
     * @param[out]    read_bytes        - Upon success, receives the number of bytes that were read; otherwise, untouched.
     * @param[in]     cpu_id            - The cpu source of the debug log.
     * @return hailo_status
     */
    hailo_status read_log(uint8_t *buffer, size_t buffer_size, size_t *read_bytes, hailo_cpu_id_t cpu_id);

    hailo_status reset_nn_core();

    /**
     * Pins a page aligned user buffer to physical memory, creates an IOMMU mapping (pci_mag_sg).
     * The buffer is used for streaming D2H or H2D using DMA.
     *
     * @param[in] data_direction - direction is used for optimization.
     * @param[in] driver_buff_handle - handle to driver allocated buffer - INVALID_DRIVER_BUFFER_HANDLE_VALUE in case
     *  of user allocated buffer
     */ 
    Expected<VdmaBufferHandle> vdma_buffer_map(void *user_address, size_t required_size, DmaDirection data_direction,
        uintptr_t driver_buff_handle);

    /**
    * Unmaps user buffer mapped using HailoRTDriver::map_buffer.
    */
    hailo_status vdma_buffer_unmap(VdmaBufferHandle handle);

    /**
     * Allocate vdma descriptors buffer that is accessable via kernel mode, user mode and the given board (using DMA).
     * 
     * @param[in] desc_count - number of descriptors to allocate. The descriptor max size is DESC_MAX_SIZE.
     * @return Upon success, returns Expected of a pair <desc_handle, dma_address>.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::pair<uintptr_t, uint64_t>> descriptors_list_create(size_t desc_count);
   
    /**
     * Frees a vdma descriptors buffer allocated by 'create_descriptors_buffer'.
     */
    hailo_status descriptors_list_release(uintptr_t desc_handle);

    /**
     * Configure vdma channel descriptors to point to the given user address.
     */
    hailo_status descriptors_list_bind_vdma_buffer(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
        uint16_t desc_page_size, uint8_t channel_index);

    Expected<uintptr_t> vdma_low_memory_buffer_alloc(size_t size);
    hailo_status vdma_low_memory_buffer_free(uintptr_t buffer_handle);

    /**
     * Allocate continuous vdma buffer.
     *
     * @param[in] size - Buffer size
     * @return pair <buffer_handle, dma_address>.
     */
    Expected<std::pair<uintptr_t, uint64_t>> vdma_continuous_buffer_alloc(size_t size);

    /**
     * Frees a vdma continuous buffer allocated by 'vdma_continuous_buffer_alloc'.
     */
    hailo_status vdma_continuous_buffer_free(uintptr_t buffer_handle);

    /**
     * The actual desc page size might be smaller than the once requested, depends on the host capabilities.
     */
    uint16_t calc_desc_page_size(uint16_t requested_size) const
    {
        if (m_desc_max_page_size < requested_size) {
            LOGGER__WARNING("Requested desc page size ({}) is bigger than max on this host ({}).",
                requested_size, m_desc_max_page_size);
        }
        return static_cast<uint16_t>(std::min(static_cast<uint32_t>(requested_size), static_cast<uint32_t>(m_desc_max_page_size)));
    }

    inline BoardType board_type() const
    {
        return m_board_type;
    }

    inline DmaType dma_type() const
    {
        return m_dma_type;
    }

    FileDescriptor& fd() {return m_fd;}

    const std::string &dev_path() const
    {
        return m_dev_path;
    }

    hailo_status mark_as_used();

    inline bool allocate_driver_buffer() const {
        return m_allocate_driver_buffer;
    }

    HailoRTDriver(const HailoRTDriver &other) = delete;
    HailoRTDriver &operator=(const HailoRTDriver &other) = delete;
    HailoRTDriver(HailoRTDriver &&other) noexcept = default;
    HailoRTDriver &operator=(HailoRTDriver &&other) = default;

    static const VdmaChannelHandle  INVALID_VDMA_CHANNEL_HANDLE;
    static const uintptr_t          INVALID_DRIVER_BUFFER_HANDLE_VALUE;
    static const uint8_t            INVALID_VDMA_CHANNEL_INDEX;

private:

    HailoRTDriver(const std::string &dev_path, FileDescriptor &&fd, hailo_status &status);

    FileDescriptor m_fd;
    std::string m_dev_path;
    uint16_t m_desc_max_page_size;
    BoardType m_board_type;
    DmaType m_dma_type;
    bool m_allocate_driver_buffer;
};

} /* namespace hailort */

#endif  /* _HAILORT_DRIVER_HPP_ */
