// SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) AND MIT
/**
 * Copyright (c) 2019-2022 Hailo Technologies Ltd. All rights reserved.
 **/

#ifndef _HAILO_IOCTL_COMMON_H_
#define _HAILO_IOCTL_COMMON_H_

// This value is not easily changeable.
// For example: the channel interrupts ioctls assume we have up to 32 channels
#define MAX_VDMA_CHANNELS_PER_ENGINE    (32)
#define MAX_VDMA_ENGINES                (3)
#define SIZE_OF_VDMA_DESCRIPTOR         (16)
#define VDMA_DEST_CHANNELS_START        (16)

#define CHANNEL_IRQ_TIMESTAMPS_SIZE (128 * 2) // Should be same as MAX_IRQ_TIMESTAMPS_SIZE (hailort_driver.hpp)
#define CHANNEL_IRQ_TIMESTAMPS_SIZE_MASK (CHANNEL_IRQ_TIMESTAMPS_SIZE - 1)

#define INVALID_CHANNEL_HANDLE_VALUE    ((uint64_t)-1)
#define INVALID_DRIVER_HANDLE_VALUE     ((uintptr_t)-1)

// Used by windows and unix driver to raise the right CPU control handle to the FW. The same as in pcie_service FW 
#define FW_ACCESS_CORE_CPU_CONTROL_SHIFT (1)
#define FW_ACCESS_CORE_CPU_CONTROL_MASK  (1 << FW_ACCESS_CORE_CPU_CONTROL_SHIFT) 
#define FW_ACCESS_CONTROL_INTERRUPT_SHIFT (0)
#define FW_ACCESS_APP_CPU_CONTROL_MASK (1 << FW_ACCESS_CONTROL_INTERRUPT_SHIFT)

#define INVALID_VDMA_CHANNEL (0xff)

#ifdef _MSC_VER
#if !defined(bool) && !defined(__cplusplus)
typedef uint8_t bool;
#endif // !defined(bool) && !defined(__cplusplus)

#if !defined(INT_MAX)
#define INT_MAX 0x7FFFFFFF
#endif // !defined(INT_MAX)

#elif defined(__linux__) // #ifdef _MSC_VER
#ifndef __KERNEL__
// include the userspace headers only if this file is included by user space program
// It is discourged to include them when compiling the driver (https://lwn.net/Articles/113349/)
#include <stdint.h>
#include <sys/types.h>
#else
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/kernel.h>
#endif // ifndef __KERNEL__

#include <linux/ioctl.h>

#define _IOW_       _IOW
#define _IOR_       _IOR
#define _IOWR_      _IOWR
#define _IO_        _IO

#define HAILO_GENERAL_IOCTL_MAGIC 'g'
#define HAILO_VDMA_IOCTL_MAGIC 'v'
#define HAILO_NON_LINUX_IOCTL_MAGIC 'w'

#elif defined(__QNX__) // #ifdef _MSC_VER
#include <devctl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/mman.h>
// defines for devctl
#define _IOW_   __DIOF
#define _IOR_   __DIOT
#define _IOWR_  __DIOTF
#define _IO_    __DION
#define HAILO_GENERAL_IOCTL_MAGIC   _DCMD_ALL
#define HAILO_VDMA_IOCTL_MAGIC      _DCMD_MISC
#define HAILO_NON_LINUX_IOCTL_MAGIC _DCMD_PROC

#else // #ifdef _MSC_VER
#error "unsupported platform!"
#endif

#pragma pack(push, 1)

struct hailo_channel_interrupt_timestamp {
    uint64_t timestamp_ns;
    uint16_t desc_num_processed;
};

// This struct is the same as `enum dma_data_direction` (defined in linux/dma-direction)
enum hailo_dma_data_direction {
    HAILO_DMA_BIDIRECTIONAL = 0,
    HAILO_DMA_TO_DEVICE = 1,
    HAILO_DMA_FROM_DEVICE = 2,
    HAILO_DMA_NONE = 3,

    /** Max enum value to maintain ABI Integrity */
    HAILO_DMA_MAX_ENUM = INT_MAX,
};

// Enum that determines if buffer should be allocated from user space or from driver
enum hailo_allocation_mode {
    HAILO_ALLOCATION_MODE_USERSPACE = 0,
    HAILO_ALLOCATION_MODE_DRIVER    = 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_ALLOCATION_MODE_MAX_ENUM = INT_MAX,
};

/* structure used in ioctl HAILO_VDMA_BUFFER_MAP */
struct hailo_vdma_buffer_map_params {
#if defined(__linux__) || defined(_MSC_VER)
    void* user_address;                             // in
#elif defined(__QNX__)
    shm_handle_t shared_memory_handle;              // in
#else
#error "unsupported platform!"
#endif // __linux__
    size_t size;                                    // in
    enum hailo_dma_data_direction data_direction;   // in
    uintptr_t allocated_buffer_handle;              // in
    size_t mapped_handle;                           // out
};

/* structure used in ioctl HAILO_VDMA_BUFFER_UNMAP */
struct hailo_vdma_buffer_unmap_params {
    size_t mapped_handle;
};

/* structure used in ioctl HAILO_DESC_LIST_CREATE */
struct hailo_desc_list_create_params {
    size_t desc_count;          // in
    uintptr_t desc_handle;      // out
    // Note: The dma address is required for CONTEXT_SWITCH firmware controls
    uint64_t dma_address;    // out
};

/* structure used in ioctl HAILO_NON_LINUX_DESC_LIST_MMAP */
struct hailo_non_linux_desc_list_mmap_params {
    uintptr_t desc_handle;  // in
    size_t size;            // in
    void* user_address;     // out
};

/* structure used in ioctl HAILO_DESC_LIST_BIND_VDMA_BUFFER */
struct hailo_desc_list_bind_vdma_buffer_params {
    size_t buffer_handle;       // in
    uintptr_t desc_handle;      // in
    uint16_t desc_page_size;    // in
    uint8_t channel_index;      // in
};

/* structure used in ioctl HAILO_VDMA_CHANNEL_ENABLE */
struct hailo_vdma_channel_enable_params {
    uint8_t engine_index;                       // in
    uint8_t channel_index;                      // in
    enum hailo_dma_data_direction direction;    // in
    // If desc_list_handle is set to valid handle (different than INVALID_DRIVER_HANDLE_VALUE),
    // the driver will start the channel with the given descriptors list.
    uintptr_t desc_list_handle;                 // in
    bool enable_timestamps_measure;             // in
    uint64_t channel_handle;                    // out
};

/* structure used in ioctl HAILO_VDMA_CHANNEL_DISABLE */
struct hailo_vdma_channel_disable_params {
    uint8_t engine_index;    // in
    uint8_t channel_index;   // in
    uint64_t channel_handle; // in
};

/* structure used in ioctl HAILO_VDMA_CHANNEL_WAIT_INT */
struct hailo_vdma_channel_wait_params {
    uint8_t engine_index;                                                               // in
    uint8_t channel_index;                                                              // in
    uint64_t channel_handle;                                                            // in
    uint64_t timeout_ms;                                                                // in
    uint32_t timestamps_count;                                                          // inout
// In linux send address to local buffer because there isnt room on stack for array
#if defined(__linux__)
    struct hailo_channel_interrupt_timestamp *timestamps;                               // out
#elif defined(__QNX__) || defined(_MSC_VER)
    struct hailo_channel_interrupt_timestamp timestamps[CHANNEL_IRQ_TIMESTAMPS_SIZE];   // out
#else
#error "unsupported platform!"
#endif // __linux__  
};

/* structure used in ioctl HAILO_VDMA_CHANNEL_ABORT */
struct hailo_vdma_channel_abort_params {
    uint8_t engine_index;       // in
    uint8_t channel_index;      // in
    uint64_t channel_handle;    // in
};

/* structure used in ioctl HAILO_VDMA_CHANNEL_CLEAR_ABORT */
struct hailo_vdma_channel_clear_abort_params {
    uint8_t engine_index;       // in
    uint8_t channel_index;      // in
    uint64_t channel_handle;    // in
};

/* structure used in ioctl HAILO_FW_CONTROL */
#define MAX_CONTROL_LENGTH  (1500)
#define PCIE_EXPECTED_MD5_LENGTH (16)


/* structure used in ioctl	HAILO_FW_CONTROL and HAILO_READ_LOG */
enum hailo_cpu_id {
    HAILO_CPU_ID_CPU0 = 0,
    HAILO_CPU_ID_CPU1,
    HAILO_CPU_ID_NONE,

    /** Max enum value to maintain ABI Integrity */
    HAILO_CPU_MAX_ENUM = INT_MAX,
};

struct hailo_fw_control {
    // expected_md5+buffer_len+buffer must be in this order at the start of the struct
    uint8_t   expected_md5[PCIE_EXPECTED_MD5_LENGTH];
    uint32_t  buffer_len;
    uint8_t   buffer[MAX_CONTROL_LENGTH];
    uint32_t timeout_ms;
    enum hailo_cpu_id cpu_id;
};

/* structure used in ioctl HAILO_BAR_TRANSFER */
// Max bar transfer size gotten from ATR0_TABLE_SIZE
#define MAX_BAR_TRANSFER_LENGTH  (4096)

enum hailo_transfer_direction {
    TRANSFER_READ = 0,
    TRANSFER_WRITE,

    /** Max enum value to maintain ABI Integrity */
    TRANSFER_MAX_ENUM = INT_MAX,
};

struct hailo_bar_transfer_params {
    enum hailo_transfer_direction transfer_direction;   // in
    uint32_t bar_index;                                 // in
    off_t offset;                                       // in
    size_t count;                                       // in
    uint8_t buffer[MAX_BAR_TRANSFER_LENGTH];            // in/out
};

/* structure used in ioctl HAILO_VDMA_CHANNEL_READ_REGISTER */
struct hailo_vdma_channel_read_register_params {
    uint8_t engine_index;                       // in
    uint8_t channel_index;                      // in
    enum hailo_dma_data_direction direction;    // in
    size_t offset;                              // in
    size_t reg_size;                            // in, can be either 1, 2 or 4
    uint32_t data;                              // out
};

/* structure used in ioctl HAILO_VDMA_CHANNEL_WRITE_REGISTER */
struct hailo_vdma_channel_write_register_params {
    uint8_t engine_index;                       // in
    uint8_t channel_index;                      // in
    enum hailo_dma_data_direction direction;    // in
    size_t offset;                              // in
    size_t reg_size;                            // in, can be either 1, 2 or 4
    uint32_t data;                              // in
};

/* structure used in ioctl HAILO_VDMA_BUFFER_SYNC */
enum hailo_vdma_buffer_sync_type {
    HAILO_SYNC_FOR_HOST,
    HAILO_SYNC_FOR_DEVICE,

    /** Max enum value to maintain ABI Integrity */
    HAILO_SYNC_MAX_ENUM = INT_MAX,
};

struct hailo_vdma_buffer_sync_params {
    size_t handle;                                      // in
    enum hailo_vdma_buffer_sync_type  sync_type;        // in
    void*                             buffer_address;   // in
    uint64_t                          buffer_size;      // in
};

/* structure used in ioctl HAILO_READ_NOTIFICATION */
#define MAX_NOTIFICATION_LENGTH  (1500)

struct hailo_d2h_notification {
    size_t buffer_len;                  // out
    uint8_t buffer[MAX_NOTIFICATION_LENGTH]; // out
};

enum hailo_board_type {
    HAILO8 = 0,
    HAILO_MERCURY,
    HAILO_BOARD_COUNT,
    HAILO_INVALID_BOARD = 0xFFFFFFFF,
};

enum hailo_dma_type {
    HAILO_DMA_TYPE_PCIE,
    HAILO_DMA_TYPE_DRAM,

    /** Max enum value to maintain ABI Integrity */
    HAILO_DMA_TYPE_MAX_ENUM = INT_MAX,
};

struct hailo_device_properties {
    uint16_t                     desc_max_page_size;
    enum hailo_board_type        board_type;
    enum hailo_allocation_mode   allocation_mode;
    enum hailo_dma_type          dma_type;
    size_t                       dma_engines_count;
#ifdef __QNX__
    pid_t                        resource_manager_pid;
#endif // __QNX__
};

struct hailo_driver_info {
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t revision_version;
};

/* structure used in ioctl HAILO_READ_LOG */
#define MAX_FW_LOG_BUFFER_LENGTH  (512)

struct hailo_read_log_params {
    enum hailo_cpu_id cpu_id;                   // in
    uint8_t buffer[MAX_FW_LOG_BUFFER_LENGTH];   // out
    size_t buffer_size;                         // in
    size_t read_bytes;                          // out
};

struct hailo_allocate_low_memory_buffer_params {
    size_t      buffer_size;    // in
    uintptr_t   buffer_handle;  // out
};

struct hailo_mark_as_in_use_params {
    bool in_use;           // out
};

struct hailo_allocate_continuous_buffer_params {
    size_t buffer_size;         // in
    uintptr_t buffer_handle;    // out
    uint64_t dma_address;       // out
};

#pragma pack(pop)

enum hailo_general_ioctl_code {
    HAILO_BAR_TRANSFER_CODE,
    HAILO_FW_CONTROL_CODE,
    HAILO_READ_NOTIFICATION_CODE,
    HAILO_DISABLE_NOTIFICATION_CODE,
    HAILO_QUERY_DEVICE_PROPERTIES_CODE,
    HAILO_QUERY_DRIVER_INFO_CODE,
    HAILO_READ_LOG_CODE,
    HAILO_RESET_NN_CORE_CODE,

    // Must be last
    HAILO_GENERAL_IOCTL_MAX_NR,
};

#define HAILO_BAR_TRANSFER              _IOWR_(HAILO_GENERAL_IOCTL_MAGIC,  HAILO_BAR_TRANSFER_CODE,               struct hailo_bar_transfer_params)
#define HAILO_FW_CONTROL                _IOWR_(HAILO_GENERAL_IOCTL_MAGIC,  HAILO_FW_CONTROL_CODE,                 struct hailo_fw_control)
#define HAILO_READ_NOTIFICATION         _IOW_(HAILO_GENERAL_IOCTL_MAGIC,   HAILO_READ_NOTIFICATION_CODE,          struct hailo_d2h_notification)
#define HAILO_DISABLE_NOTIFICATION      _IO_(HAILO_GENERAL_IOCTL_MAGIC,    HAILO_DISABLE_NOTIFICATION_CODE)
#define HAILO_QUERY_DEVICE_PROPERTIES   _IOW_(HAILO_GENERAL_IOCTL_MAGIC,   HAILO_QUERY_DEVICE_PROPERTIES_CODE,    struct hailo_device_properties)
#define HAILO_QUERY_DRIVER_INFO         _IOW_(HAILO_GENERAL_IOCTL_MAGIC,   HAILO_QUERY_DRIVER_INFO_CODE,          struct hailo_driver_info)
#define HAILO_READ_LOG                  _IOWR_(HAILO_GENERAL_IOCTL_MAGIC,  HAILO_READ_LOG_CODE,                   struct hailo_read_log_params)
#define HAILO_RESET_NN_CORE             _IO_(HAILO_GENERAL_IOCTL_MAGIC,    HAILO_RESET_NN_CORE_CODE)

enum hailo_vdma_ioctl_code {
    HAILO_VDMA_CHANNEL_ENABLE_CODE,
    HAILO_VDMA_CHANNEL_DISABLE_CODE,
    HAILO_VDMA_CHANNEL_WAIT_INT_CODE,
    HAILO_VDMA_CHANNEL_ABORT_CODE,
    HAILO_VDMA_CHANNEL_CLEAR_ABORT_CODE,
    HAILO_VDMA_CHANNEL_READ_REGISTER_CODE,
    HAILO_VDMA_CHANNEL_WRITE_REGISTER_CODE,
    HAILO_VDMA_BUFFER_MAP_CODE,
    HAILO_VDMA_BUFFER_UNMAP_CODE,
    HAILO_VDMA_BUFFER_SYNC_CODE,
    HAILO_DESC_LIST_CREATE_CODE,
    HAILO_DESC_LIST_RELEASE_CODE,
    HAILO_DESC_LIST_BIND_VDMA_BUFFER_CODE,
    HAILO_VDMA_LOW_MEMORY_BUFFER_ALLOC_CODE,
    HAILO_VDMA_LOW_MEMORY_BUFFER_FREE_CODE,
    HAILO_MARK_AS_IN_USE_CODE,
    HAILO_VDMA_CONTINUOUS_BUFFER_ALLOC_CODE,
    HAILO_VDMA_CONTINUOUS_BUFFER_FREE_CODE,

    // Must be last
    HAILO_VDMA_IOCTL_MAX_NR,
};

#define HAILO_VDMA_CHANNEL_ENABLE           _IOWR_(HAILO_VDMA_IOCTL_MAGIC, HAILO_VDMA_CHANNEL_ENABLE_CODE,        struct hailo_vdma_channel_enable_params)
#define HAILO_VDMA_CHANNEL_DISABLE          _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_VDMA_CHANNEL_DISABLE_CODE,       struct hailo_vdma_channel_disable_params)
#define HAILO_VDMA_CHANNEL_WAIT_INT         _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_VDMA_CHANNEL_WAIT_INT_CODE,      struct hailo_vdma_channel_wait_params)
#define HAILO_VDMA_CHANNEL_ABORT            _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_VDMA_CHANNEL_ABORT_CODE,         struct hailo_vdma_channel_abort_params)
#define HAILO_VDMA_CHANNEL_CLEAR_ABORT      _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_VDMA_CHANNEL_CLEAR_ABORT_CODE,   struct hailo_vdma_channel_clear_abort_params)
#define HAILO_VDMA_CHANNEL_READ_REGISTER    _IOWR_(HAILO_VDMA_IOCTL_MAGIC, HAILO_VDMA_CHANNEL_READ_REGISTER_CODE, struct hailo_vdma_channel_read_register_params)
#define HAILO_VDMA_CHANNEL_WRITE_REGISTER   _IOR_(HAILO_VDMA_IOCTL_MAGIC, HAILO_VDMA_CHANNEL_WRITE_REGISTER_CODE, struct hailo_vdma_channel_write_register_params)

#define HAILO_VDMA_BUFFER_MAP               _IOWR_(HAILO_VDMA_IOCTL_MAGIC, HAILO_VDMA_BUFFER_MAP_CODE,            struct hailo_vdma_buffer_map_params)
#define HAILO_VDMA_BUFFER_UNMAP             _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_VDMA_BUFFER_UNMAP_CODE,          struct hailo_vdma_buffer_unmap_params)
#define HAILO_VDMA_BUFFER_SYNC              _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_VDMA_BUFFER_SYNC_CODE,           struct hailo_vdma_buffer_sync_params)

#define HAILO_DESC_LIST_CREATE              _IOWR_(HAILO_VDMA_IOCTL_MAGIC, HAILO_DESC_LIST_CREATE_CODE,           struct hailo_desc_list_create_params)
#define HAILO_DESC_LIST_RELEASE             _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_DESC_LIST_RELEASE_CODE,          uintptr_t)
#define HAILO_DESC_LIST_BIND_VDMA_BUFFER    _IOR_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_DESC_LIST_BIND_VDMA_BUFFER_CODE, struct hailo_desc_list_bind_vdma_buffer_params)

#define HAILO_VDMA_LOW_MEMORY_BUFFER_ALLOC  _IOWR_(HAILO_VDMA_IOCTL_MAGIC, HAILO_VDMA_LOW_MEMORY_BUFFER_ALLOC_CODE, struct hailo_allocate_low_memory_buffer_params)
#define HAILO_VDMA_LOW_MEMORY_BUFFER_FREE   _IO_(HAILO_VDMA_IOCTL_MAGIC,   HAILO_VDMA_LOW_MEMORY_BUFFER_FREE_CODE)

#define HAILO_MARK_AS_IN_USE                _IOW_(HAILO_VDMA_IOCTL_MAGIC,  HAILO_MARK_AS_IN_USE_CODE,             struct hailo_mark_as_in_use_params)

#define HAILO_VDMA_CONTINUOUS_BUFFER_ALLOC  _IOWR_(HAILO_VDMA_IOCTL_MAGIC, HAILO_VDMA_CONTINUOUS_BUFFER_ALLOC_CODE, struct hailo_allocate_continuous_buffer_params)
#define HAILO_VDMA_CONTINUOUS_BUFFER_FREE   _IO_(HAILO_VDMA_IOCTL_MAGIC,   HAILO_VDMA_CONTINUOUS_BUFFER_FREE_CODE)


enum hailo_non_linux_ioctl_code {
    HAILO_NON_LINUX_DESC_LIST_MMAP_CODE,

    // Must be last
    HAILO_NON_LINUX_IOCTL_MAX_NR,
};

#define HAILO_NON_LINUX_DESC_LIST_MMAP _IOWR_(HAILO_NON_LINUX_IOCTL_MAGIC, HAILO_NON_LINUX_DESC_LIST_MMAP_CODE, struct hailo_non_linux_desc_list_mmap_params)


#endif /* _HAILO_IOCTL_COMMON_H_ */
