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

#include "common/utils.hpp"

#include "os/file_descriptor.hpp"
#include "vdma/channel/channel_id.hpp"

#include <mutex>
#include <thread>
#include <chrono>
#include <utility>
#include <array>

#ifdef __QNX__
#include <sys/mman.h>
#endif // __QNX__


namespace hailort
{

#define DEVICE_NODE_NAME       "hailo"

#define PENDING_BUFFERS_SIZE (128)
static_assert((0 == ((PENDING_BUFFERS_SIZE - 1) & PENDING_BUFFERS_SIZE)), "PENDING_BUFFERS_SIZE must be a power of 2");

#define MIN_ACTIVE_TRANSFERS_SCALE (2)
#define MAX_ACTIVE_TRANSFERS_SCALE (4)

#define HAILO_MAX_BATCH_SIZE ((PENDING_BUFFERS_SIZE / MIN_ACTIVE_TRANSFERS_SCALE) - 1)

// When measuring latency, each channel is capable of PENDING_BUFFERS_SIZE active transfers, each transfer raises max of 2 timestamps
#define MAX_IRQ_TIMESTAMPS_SIZE (PENDING_BUFFERS_SIZE * 2)

#define PCIE_EXPECTED_MD5_LENGTH (16)

constexpr size_t VDMA_CHANNELS_PER_ENGINE = 32;
constexpr size_t MAX_VDMA_ENGINES_COUNT = 3;
constexpr size_t MAX_VDMA_CHANNELS_COUNT = MAX_VDMA_ENGINES_COUNT * VDMA_CHANNELS_PER_ENGINE;
constexpr uint8_t MIN_H2D_CHANNEL_INDEX = 0;
constexpr uint8_t MAX_H2D_CHANNEL_INDEX = 15;
constexpr uint8_t MIN_D2H_CHANNEL_INDEX = MAX_H2D_CHANNEL_INDEX + 1;
constexpr uint8_t MAX_D2H_CHANNEL_INDEX = 31;

constexpr size_t SIZE_OF_SINGLE_DESCRIPTOR = 0x10;

// NOTE: don't change members from this struct without updating all code using it (platform specific)
struct ChannelInterruptTimestamp {
    std::chrono::nanoseconds timestamp;
    uint16_t desc_num_processed;
};

struct ChannelInterruptTimestampList {
    ChannelInterruptTimestamp timestamp_list[MAX_IRQ_TIMESTAMPS_SIZE];
    size_t count;
};

struct ChannelIrqData {
    vdma::ChannelId channel_id;
    bool is_active;
    uint16_t desc_num_processed;
    uint8_t host_error;
    uint8_t device_error;
};

struct IrqData {
    uint8_t channels_count;
    std::array<ChannelIrqData, MAX_VDMA_CHANNELS_COUNT> channels_irq_data;
};

// Bitmap per engine
using ChannelsBitmap = std::array<uint32_t, MAX_VDMA_ENGINES_COUNT>;

#if defined(__linux__) || defined(_MSC_VER)
// Unique handle returned from the driver.
using vdma_mapped_buffer_driver_identifier = uintptr_t;
#elif defined(__QNX__)
// Identifier is the shared memory file descriptor.
using vdma_mapped_buffer_driver_identifier = int;
#else
#error "unsupported platform!"
#endif // defined(__linux__) || defined(_MSC_VER)

struct DescriptorsListInfo {
    uintptr_t handle; // Unique identifier for the driver.
    uint64_t dma_address;
    size_t desc_count;
    void *user_address;
};

class HailoRTDriver final
{
public:

    struct DeviceInfo {
        std::string dev_path;
        std::string device_id;
    };

    enum class DmaDirection {
        H2D = 0,
        D2H,
        BOTH
    };

    enum class DmaSyncDirection {
        TO_HOST = 0,
        TO_DEVICE
    };

    enum class DmaType {
        PCIE,
        DRAM
    };

    enum class MemoryType {
        DIRECT_MEMORY,

        // vDMA memories
        VDMA0,  // On PCIe board, VDMA0 and BAR2 are the same
        VDMA1,
        VDMA2,

        // PCIe driver memories
        PCIE_BAR0,
        PCIE_BAR2,
        PCIE_BAR4,

        // DRAM DMA driver memories
        DMA_ENGINE0,
        DMA_ENGINE1,
        DMA_ENGINE2,
    };

    using VdmaBufferHandle = size_t;

    static Expected<HailoRTDriver> create(const DeviceInfo &device_info);

// TODO: HRT-7309 add implementation for Windows
#if defined(__linux__) || defined(__QNX__)
    static hailo_status hailo_ioctl(int fd, int request, void* request_struct, int &error_status);
#endif // defined(__linux__) || defined(__QNX__)

    static Expected<std::vector<DeviceInfo>> scan_devices();

    hailo_status read_memory(MemoryType memory_type, uint64_t address, void *buf, size_t size);
    hailo_status write_memory(MemoryType memory_type, uint64_t address, const void *buf, size_t size);

    Expected<uint32_t> read_vdma_channel_register(vdma::ChannelId channel_id, DmaDirection data_direction, size_t offset,
        size_t reg_size);
    hailo_status write_vdma_channel_register(vdma::ChannelId channel_id, DmaDirection data_direction, size_t offset,
        size_t reg_size, uint32_t data);

    hailo_status vdma_buffer_sync(VdmaBufferHandle buffer, DmaSyncDirection sync_direction, size_t offset, size_t count);

    hailo_status vdma_interrupts_enable(const ChannelsBitmap &channels_bitmap, bool enable_timestamps_measure);
    hailo_status vdma_interrupts_disable(const ChannelsBitmap &channel_id);
    Expected<IrqData> vdma_interrupts_wait(const ChannelsBitmap &channels_bitmap);
    Expected<ChannelInterruptTimestampList> vdma_interrupts_read_timestamps(vdma::ChannelId channel_id);

    Expected<std::vector<uint8_t>> read_notification();
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
        const vdma_mapped_buffer_driver_identifier &driver_buff_handle);

    /**
    * Unmaps user buffer mapped using HailoRTDriver::map_buffer.
    */
    hailo_status vdma_buffer_unmap(VdmaBufferHandle handle);

    /**
     * Allocate vdma descriptors list object that can bind to some buffer. Used for scatter gather vdma.
     *
     * @param[in] desc_count - number of descriptors to allocate. The descriptor max size is DESC_MAX_SIZE.
     * @param[in] is_circular - if true, the descriptors list can be used in a circular (and desc_count must be power
     *                          of 2)
     */
    Expected<DescriptorsListInfo> descriptors_list_create(size_t desc_count, bool is_circular);

    /**
     * Frees a vdma descriptors buffer allocated by 'descriptors_list_create'.
     */
    hailo_status descriptors_list_release(const DescriptorsListInfo &descriptors_list_info);

    /**
     * Configure vdma channel descriptors to point to the given user address.
     */
    hailo_status descriptors_list_bind_vdma_buffer(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
        uint16_t desc_page_size, uint8_t channel_index, uint32_t starting_desc);

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
     * Marks the device as used for vDMA operations. Only one open FD can be marked at once.
     * The device is "unmarked" only on FD close.
     */
    hailo_status mark_as_used();

    const std::string &device_id() const
    {
        return m_device_info.device_id;
    }

    inline DmaType dma_type() const
    {
        return m_dma_type;
    }

    FileDescriptor& fd() {return m_fd;}

    inline bool allocate_driver_buffer() const
    {
        return m_allocate_driver_buffer;
    }

    inline uint16_t desc_max_page_size() const
    {
        return m_desc_max_page_size;
    }

    inline size_t dma_engines_count() const
    {
        return m_dma_engines_count;
    }

    inline bool is_fw_loaded() const
    {
        return m_is_fw_loaded;
    }

    HailoRTDriver(const HailoRTDriver &other) = delete;
    HailoRTDriver &operator=(const HailoRTDriver &other) = delete;
    HailoRTDriver(HailoRTDriver &&other) noexcept = default;
    HailoRTDriver &operator=(HailoRTDriver &&other) = default;

    static const uintptr_t INVALID_DRIVER_BUFFER_HANDLE_VALUE;
    static const size_t INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE;
    static const uint8_t INVALID_VDMA_CHANNEL_INDEX;

private:
    hailo_status read_memory_ioctl(MemoryType memory_type, uint64_t address, void *buf, size_t size);
    hailo_status write_memory_ioctl(MemoryType memory_type, uint64_t address, const void *buf, size_t size);

    Expected<std::pair<uintptr_t, uint64_t>> descriptors_list_create_ioctl(size_t desc_count, bool is_circular);
    hailo_status descriptors_list_release_ioctl(uintptr_t desc_handle);
    Expected<void *> descriptors_list_create_mmap(uintptr_t desc_handle, size_t desc_count);
    hailo_status descriptors_list_create_munmap(void *address, size_t desc_count);

    HailoRTDriver(const DeviceInfo &device_info, FileDescriptor &&fd, hailo_status &status);

    bool is_valid_channel_id(const vdma::ChannelId &channel_id);
    bool is_valid_channels_bitmap(const ChannelsBitmap &bitmap)
    {
        for (size_t engine_index = m_dma_engines_count; engine_index < MAX_VDMA_ENGINES_COUNT; engine_index++) {
            if (bitmap[engine_index]) {
                LOGGER__ERROR("Engine {} does not exist on device (engines count {})", engine_index,
                    m_dma_engines_count);
                return false;
            }
        }
        return true;
    }

    FileDescriptor m_fd;
    DeviceInfo m_device_info;
    uint16_t m_desc_max_page_size;
    DmaType m_dma_type;
    bool m_allocate_driver_buffer;
    size_t m_dma_engines_count;
    bool m_is_fw_loaded;
#ifdef __QNX__
    pid_t m_resource_manager_pid;
#endif // __QNX__
};

} /* namespace hailort */

#endif  /* _HAILORT_DRIVER_HPP_ */
