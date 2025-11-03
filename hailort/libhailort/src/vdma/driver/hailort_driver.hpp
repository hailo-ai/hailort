/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "common/file_descriptor.hpp"
#include "vdma/channel/channel_id.hpp"

#include <mutex>
#include <thread>
#include <chrono>
#include <utility>
#include <array>
#include <list>
#include <cerrno>

#ifdef __QNX__
#include <sys/mman.h>
#endif // __QNX__


namespace hailort
{

#define DEVICE_NODE_NAME       "hailo"

constexpr size_t ONGOING_TRANSFERS_SIZE = 128;
static_assert((0 == ((ONGOING_TRANSFERS_SIZE - 1) & ONGOING_TRANSFERS_SIZE)), "ONGOING_TRANSFERS_SIZE must be a power of 2");

#define MIN_ACTIVE_TRANSFERS_SCALE (2)

#if defined(_WIN32)
#define MAX_ACTIVE_TRANSFERS_SCALE (8)
#else
#define MAX_ACTIVE_TRANSFERS_SCALE (32)
#endif

#define HAILO_MAX_BATCH_SIZE ((ONGOING_TRANSFERS_SIZE / MIN_ACTIVE_TRANSFERS_SCALE) - 1)

// When measuring latency, each channel is capable of ONGOING_TRANSFERS_SIZE active transfers, each transfer raises max of 2 timestamps
#define MAX_IRQ_TIMESTAMPS_SIZE (ONGOING_TRANSFERS_SIZE * 2)

#define PCIE_EXPECTED_MD5_LENGTH (16)

constexpr size_t MAX_VDMA_ENGINES_COUNT             = 3;
// For all archs except for Hailo12L
constexpr size_t VDMA_CHANNELS_PER_ENGINE           = 40;
constexpr size_t MAX_VDMA_CHANNELS_COUNT            = MAX_VDMA_ENGINES_COUNT * VDMA_CHANNELS_PER_ENGINE;
constexpr uint8_t MIN_H2D_CHANNEL_INDEX             = 0;
constexpr uint8_t MAX_H2D_CHANNEL_INDEX             = 15;
constexpr uint8_t MIN_D2H_CHANNEL_INDEX             = MAX_H2D_CHANNEL_INDEX + 1;
constexpr uint8_t MAX_D2H_CHANNEL_INDEX             = 31;
constexpr uint8_t MIN_ENHANCED_D2H_CHANNEL_INDEX    = 28;

// For Hailo12L
constexpr uint8_t MAX_H2D_CHANNEL_INDEX_H12L             = 23;
constexpr uint8_t MIN_D2H_CHANNEL_INDEX_H12L             = MAX_H2D_CHANNEL_INDEX_H12L + 1;
constexpr uint8_t MAX_D2H_CHANNEL_INDEX_H12L             = 39;
constexpr uint8_t MIN_ENHANCED_D2H_CHANNEL_INDEX_H12L    = MIN_D2H_CHANNEL_INDEX_H12L;

constexpr size_t MAX_TRANSFER_BUFFERS_IN_REQUEST = 8;

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
    uint8_t transfers_completed;
    bool validation_success;
};

struct IrqData {
    uint8_t channels_count;
    std::array<ChannelIrqData, MAX_VDMA_CHANNELS_COUNT> channels_irq_data;
};

// Contstant parameters for descriptors page size and count.
// May be different between descriptors type (CCB vs SG), between devices
// and even between different hosts (some hosts may use smaller page size).
struct DescSizesParams {
    uint16_t default_page_size;
    uint16_t min_page_size;
    uint16_t max_page_size;

    uint32_t min_descs_count;
    uint32_t max_descs_count;
};

// Bitmap per engine
using ChannelsBitmap = std::array<uint64_t, MAX_VDMA_ENGINES_COUNT>;

struct DescriptorsListInfo {
    uintptr_t handle; // Unique identifier for the driver.
    uint64_t dma_address;
};

struct ContinousBufferInfo {
    uintptr_t handle;  // Unique identifer for the driver.
    uint64_t dma_address;
    size_t size;
    void *user_address;
};

enum class InterruptsDomain
{
    NONE    = 0,
    DEVICE  = 1 << 0,
    HOST    = 1 << 1,
    BOTH    = DEVICE | HOST
};

inline InterruptsDomain operator|(InterruptsDomain a, InterruptsDomain b)
{
    return static_cast<InterruptsDomain>(static_cast<int>(a) | static_cast<int>(b));
}

inline InterruptsDomain& operator|=(InterruptsDomain &a, InterruptsDomain b)
{
    a = a | b;
    return a;
}

class HailoRTDriver final
{
public:

    enum class AcceleratorType {
        NNC_ACCELERATOR,
        SOC_ACCELERATOR,
        ACC_TYPE_MAX_VALUE
    };

    struct DeviceInfo {
        std::string dev_path;
        std::string device_id;
        enum AcceleratorType accelerator_type;
    };

    enum class DmaDirection {
        H2D = 0,
        D2H,
        BOTH
    };

    enum class DmaBufferType {
        USER_PTR_BUFFER = 0,
        DMABUF_BUFFER
    };

    enum class DmaSyncDirection {
        TO_HOST = 0,
        TO_DEVICE
    };

    enum class DmaType {
        PCIE,
        DRAM,
        PCIE_EP
    };

    // Should match enum hailo_board_type
    enum class DeviceBoardType {
        DEVICE_BOARD_TYPE_HAILO8 = 0,
        DEVICE_BOARD_TYPE_HAILO15,
        DEVICE_BOARD_TYPE_HAILO15L,
        DEVICE_BOARD_TYPE_HAILO10H,
        DEVICE_BOARD_TYPE_HAILO15H_ACCELERATOR_MODE,
        DEVICE_BOARD_TYPE_MARS,
        DEVICE_BOARD_TYPE_COUNT,
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

        // PCIe EP driver memories
        PCIE_EP_CONFIG,
        PCIE_EP_BRIDGE,
    };

    enum class PcieSessionType {
        CLIENT,   // (soc)
        SERVER  // (A53 - pci_ep)
    };

    using VdmaBufferHandle = size_t;

    static Expected<std::unique_ptr<HailoRTDriver>> create(const std::string &device_id, const std::string &dev_path);

    static Expected<std::unique_ptr<HailoRTDriver>> create_pcie(const std::string &device_id);

    static Expected<std::unique_ptr<HailoRTDriver>> create_integrated_nnc();
    static bool is_integrated_nnc_loaded();

    static Expected<std::unique_ptr<HailoRTDriver>> create_pcie_ep();
    static bool is_pcie_ep_loaded();

    static Expected<std::vector<DeviceInfo>> scan_devices();
    static Expected<std::vector<DeviceInfo>> scan_devices(AcceleratorType accelerator_type);

    hailo_status vdma_enable_channels(const ChannelsBitmap &channels_bitmap, bool enable_timestamps_measure);
    hailo_status vdma_disable_channels(const ChannelsBitmap &channel_id);
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

    hailo_status reset_chip();

    Expected<uint64_t> write_action_list(uint8_t *data, size_t size);

    /**
     * Pins a page aligned user buffer to physical memory, creates an IOMMU mapping (pci_mag_sg).
     * The buffer is used for streaming D2H or H2D using DMA.
     */
    Expected<VdmaBufferHandle> vdma_buffer_map(uintptr_t addr_or_fd, size_t size, DmaDirection direction,
        DmaBufferType type);

    /**
    * Unmaps user buffer mapped using HailoRTDriver::map_buffer.
    */
    hailo_status vdma_buffer_unmap(uintptr_t addr_or_fd, size_t size, DmaDirection direction, DmaBufferType type);

    hailo_status vdma_buffer_sync(VdmaBufferHandle buffer, DmaSyncDirection sync_direction, size_t offset, size_t count);

    /**
     * Allocate vdma descriptors list object that can bind to some buffer. Used for scatter gather vdma.
     *
     * @param[in] desc_count - number of descriptors to allocate. The descriptor max size is desc_page_size.
     * @param[in] desc_page_size - maximum size of each descriptor. Must be a power of 2.
     * @param[in] is_circular - if true, the descriptors list can be used in a circular (and desc_count must be power
     *                          of 2)
     */
    Expected<DescriptorsListInfo> descriptors_list_create(size_t desc_count, uint16_t desc_page_size,
        bool is_circular);

    /**
     * Frees a vdma descriptors buffer allocated by 'descriptors_list_create'.
     */
    hailo_status descriptors_list_release(const DescriptorsListInfo &descriptors_list_info);

    /**
     * Program the given descriptors list to point to the given buffer.
     */
    hailo_status descriptors_list_program(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
        size_t buffer_offset, size_t transfer_size, uint32_t transfers_count, uint8_t channel_index,
        uint32_t starting_desc, InterruptsDomain last_desc_interrupts);

    struct TransferBuffer {
        bool is_dma_buf;
        uintptr_t addr_or_fd;
        size_t size;
    };

    /**
     * Prepares a transfer for launch.
     * Mapping the transfer buffers
     * Binding the transfer buffers to the descriptor list
     * Synchronizing the transfer buffers
     */
    hailo_status cancel_prepared_transfers(uintptr_t desc_handle);

    hailo_status hailo_vdma_prepare_transfer(vdma::ChannelId channel_id, uintptr_t desc_handle,
        const std::vector<TransferBuffer> &transfer_buffers, InterruptsDomain first_interrupts_domain,
        InterruptsDomain last_desc_interrupts);

    /**
     * Launches some transfer on the given channel.
     * The maximum number of transfer buffers is MAX_TRANSFER_BUFFERS_IN_REQUEST.
     */
    hailo_status launch_transfer(vdma::ChannelId channel_id, uintptr_t desc_handle,
        const std::vector<TransferBuffer> &transfer_buffer, bool is_cyclic,
        InterruptsDomain first_desc_interrupts, InterruptsDomain last_desc_interrupts);

    /**
     * Allocate continuous vdma buffer.
     *
     * @param[in] size - Buffer size
     * @return pair <buffer_handle, dma_address>.
     */
    Expected<ContinousBufferInfo> vdma_continuous_buffer_alloc(size_t size);

    /**
     * Frees a vdma continuous buffer allocated by 'vdma_continuous_buffer_alloc'.
     */
    hailo_status vdma_continuous_buffer_free(const ContinousBufferInfo &buffer_info);

    /**
     * Marks the device as used for vDMA operations. Only one open FD can be marked at once.
     * The device is "unmarked" only on FD close.
     */
    hailo_status mark_as_used();

    Expected<std::pair<vdma::ChannelId, vdma::ChannelId>> soc_connect(uint16_t port_number,
        uintptr_t input_buffer_desc_handle, uintptr_t output_buffer_desc_handle);

    Expected<std::pair<vdma::ChannelId, vdma::ChannelId>> pci_ep_accept(uint16_t port_number,
        uintptr_t input_buffer_desc_handle, uintptr_t output_buffer_desc_handle);

    hailo_status pci_ep_listen(uint16_t port_number, uint8_t backlog_size);

    hailo_status close_connection(vdma::ChannelId input_channel, vdma::ChannelId output_channel,
        PcieSessionType session_type);

    const std::string& device_id() const
    {
        return m_device_id;
    }

    inline DmaType dma_type() const
    {
        return m_dma_type;
    }

    inline DeviceBoardType board_type() const
    {
        return m_board_type;
    }

    FileDescriptor& fd() {return m_fd;}

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

    DescSizesParams get_sg_desc_params() const;
    DescSizesParams get_ccb_desc_params() const;

    HailoRTDriver(const HailoRTDriver &other) = delete;
    HailoRTDriver &operator=(const HailoRTDriver &other) = delete;
    HailoRTDriver(HailoRTDriver &&other) noexcept = delete;
    HailoRTDriver &operator=(HailoRTDriver &&other) = delete;

    static const uintptr_t INVALID_DRIVER_BUFFER_HANDLE_VALUE;
    static const size_t INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE;
    static const uint8_t INVALID_VDMA_CHANNEL_INDEX;

    static constexpr const char *INTEGRATED_NNC_DEVICE_ID = "[integrated]";
    static constexpr const char *PCIE_EP_DEVICE_ID = "[pci_ep]";

private:
    template<typename PointerType>
    int run_ioctl(uint32_t ioctl_code, PointerType param);

    Expected<std::pair<uintptr_t, uint64_t>> continous_buffer_alloc_ioctl(size_t size);
    hailo_status continous_buffer_free_ioctl(uintptr_t desc_handle);
    Expected<void *> continous_buffer_mmap(uintptr_t desc_handle, size_t size);
    hailo_status continous_buffer_munmap(void *address, size_t size);

    HailoRTDriver(const std::string &device_id, FileDescriptor &&fd, hailo_status &status);

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
    std::string m_device_id;
    uint16_t m_desc_max_page_size;
    DmaType m_dma_type;
    size_t m_dma_engines_count;
    DeviceBoardType m_board_type;
    bool m_is_fw_loaded;
#ifdef __QNX__
    pid_t m_resource_manager_pid;
#endif // __QNX__

#ifdef __linux__
    // TODO: HRT-11595 fix linux driver deadlock and remove the mutex.
    // Currently, on the linux, the mmap syscall is called under current->mm lock held. Inside, we lock the board
    // mutex. On other ioctls, we first lock the board mutex, and then lock current->mm mutex (For example - before
    // pinning user address to memory and on copy_to_user/copy_from_user calls).
    // Need to refactor the driver lock mechanism and then remove the mutex from here.
    std::mutex m_driver_lock;
#endif
};

inline hailo_dma_buffer_direction_t to_hailo_dma_direction(HailoRTDriver::DmaDirection dma_direction)
{
    return (dma_direction == HailoRTDriver::DmaDirection::H2D)  ? HAILO_DMA_BUFFER_DIRECTION_H2D :
           (dma_direction == HailoRTDriver::DmaDirection::D2H)  ? HAILO_DMA_BUFFER_DIRECTION_D2H :
           (dma_direction == HailoRTDriver::DmaDirection::BOTH) ? HAILO_DMA_BUFFER_DIRECTION_BOTH :
                                                                  HAILO_DMA_BUFFER_DIRECTION_MAX_ENUM;
}

inline HailoRTDriver::DmaDirection to_hailo_driver_direction(hailo_dma_buffer_direction_t dma_direction)
{
    assert(dma_direction <= HAILO_DMA_BUFFER_DIRECTION_BOTH);
    return (dma_direction == HAILO_DMA_BUFFER_DIRECTION_H2D)  ? HailoRTDriver::DmaDirection::H2D :
           (dma_direction == HAILO_DMA_BUFFER_DIRECTION_D2H)  ? HailoRTDriver::DmaDirection::D2H :
                                                                HailoRTDriver::DmaDirection::BOTH;
}

} /* namespace hailort */

#endif  /* _HAILORT_DRIVER_HPP_ */
