#include "os/hailort_driver.hpp"
#include "os/driver_scan.hpp"
#include "hailo_ioctl_common.h"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

namespace hailort
{

static_assert(VDMA_CHANNELS_PER_ENGINE == MAX_VDMA_CHANNELS_PER_ENGINE, "Driver and libhailort parameters mismatch");
static_assert(MAX_VDMA_ENGINES == MAX_VDMA_ENGINES_COUNT, "Driver and libhailort parameters mismatch");
static_assert(MIN_D2H_CHANNEL_INDEX == VDMA_DEST_CHANNELS_START, "Driver and libhailort parameters mismatch");

static hailo_dma_data_direction direction_to_dma_data_direction(HailoRTDriver::DmaDirection direction) {
    switch (direction) {
    case HailoRTDriver::DmaDirection::H2D:
        return HAILO_DMA_TO_DEVICE;
    case HailoRTDriver::DmaDirection::D2H:
        return HAILO_DMA_FROM_DEVICE;
    case HailoRTDriver::DmaDirection::BOTH:
        return HAILO_DMA_BIDIRECTIONAL;
    }

    assert(false);
    // On release build Return value that will make ioctls to fail.
    return HAILO_DMA_NONE;
}

static enum hailo_cpu_id translate_cpu_id(hailo_cpu_id_t cpu_id)
{   
    switch (cpu_id) {
    case HAILO_CPU_ID_0:
        return HAILO_CPU_ID_CPU0;
    case HAILO_CPU_ID_1:
        return HAILO_CPU_ID_CPU1;
    case HAILO_CPU_ID_MAX_ENUM:
        // Add label for HAILO_CPU_ID_MAX_ENUM to cover all enum cases (avoid warnings). Continue to the assert.
        break;
    }

    assert(false);
    // On release build Return value that will make ioctls to fail.
    return HAILO_CPU_ID_NONE;
}

static hailo_transfer_memory_type translate_memory_type(HailoRTDriver::MemoryType memory_type)
{
    using MemoryType = HailoRTDriver::MemoryType;
    switch (memory_type) {
    case MemoryType::DIRECT_MEMORY:
        return HAILO_TRANSFER_DEVICE_DIRECT_MEMORY;
    case MemoryType::VDMA0:
        return HAILO_TRANSFER_MEMORY_VDMA0;
    case MemoryType::VDMA1:
        return HAILO_TRANSFER_MEMORY_VDMA1;
    case MemoryType::VDMA2:
        return HAILO_TRANSFER_MEMORY_VDMA2;
    case MemoryType::PCIE_BAR0:
        return HAILO_TRANSFER_MEMORY_PCIE_BAR0;
    case MemoryType::PCIE_BAR2:
        return HAILO_TRANSFER_MEMORY_PCIE_BAR2;
    case MemoryType::PCIE_BAR4:
        return HAILO_TRANSFER_MEMORY_PCIE_BAR4;
    case MemoryType::DMA_ENGINE0:
        return HAILO_TRANSFER_MEMORY_DMA_ENGINE0;
    case MemoryType::DMA_ENGINE1:
        return HAILO_TRANSFER_MEMORY_DMA_ENGINE1;
    case MemoryType::DMA_ENGINE2:
        return HAILO_TRANSFER_MEMORY_DMA_ENGINE2;
    }

    assert(false);
    return HAILO_TRANSFER_MEMORY_MAX_ENUM;
}

static Expected<ChannelInterruptTimestampList> create_interrupt_timestamp_list(
    hailo_vdma_interrupts_read_timestamp_params &inter_data)
{
    CHECK_AS_EXPECTED(inter_data.timestamps_count <= MAX_IRQ_TIMESTAMPS_SIZE, HAILO_DRIVER_FAIL,
        "Invalid channel interrupts timestamps count returned {}", inter_data.timestamps_count);
    ChannelInterruptTimestampList timestamp_list{};

    timestamp_list.count = inter_data.timestamps_count;
    for (size_t i = 0; i < timestamp_list.count; i++) {
        timestamp_list.timestamp_list[i].timestamp = std::chrono::nanoseconds(inter_data.timestamps[i].timestamp_ns);
        timestamp_list.timestamp_list[i].desc_num_processed = inter_data.timestamps[i].desc_num_processed;
    }
    return timestamp_list;
}

// TODO: validate wraparounds for buffer/mapping handles in the driver (HRT-9509)
const uintptr_t HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE = INVALID_DRIVER_HANDLE_VALUE;
const size_t HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE = INVALID_DRIVER_HANDLE_VALUE;
const uint8_t HailoRTDriver::INVALID_VDMA_CHANNEL_INDEX = INVALID_VDMA_CHANNEL;

Expected<std::unique_ptr<HailoRTDriver>> HailoRTDriver::create(const DeviceInfo &device_info)
{
    auto fd = FileDescriptor(open(device_info.dev_path.c_str(), O_RDWR));
    CHECK_AS_EXPECTED(fd >= 0, HAILO_DRIVER_FAIL,
        "Failed to open device file {} with error {}", device_info.dev_path, errno);

    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<HailoRTDriver> driver(new (std::nothrow) HailoRTDriver(device_info, std::move(fd), status));
    CHECK_NOT_NULL_AS_EXPECTED(driver, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return driver;
}

#if defined(__linux__)
static bool is_blocking_ioctl(unsigned long request)
{
    switch (request) {
    case HAILO_VDMA_INTERRUPTS_WAIT:
    case HAILO_FW_CONTROL:
    case HAILO_READ_NOTIFICATION:
        return true;
    default:
        return false;
    }
}

hailo_status HailoRTDriver::hailo_ioctl(int fd, unsigned long request, void* request_struct, int &error_status)
{
    // We lock m_driver lock on all request but the blocking onces. Read m_driver_lock doc in the header
    std::unique_lock<std::mutex> lock;
    if (!is_blocking_ioctl(request)) {
        lock = std::unique_lock<std::mutex>(m_driver_lock);
    }

    int res = ioctl(fd, request, request_struct);
    error_status = errno;
    return (res >= 0) ? HAILO_SUCCESS : HAILO_DRIVER_FAIL;
}
#elif defined(__QNX__)
hailo_status HailoRTDriver::hailo_ioctl(int fd, unsigned long request, void* request_struct, int &error_status)
{
    int res = ioctl(fd, static_cast<int>(request), request_struct);
    if (0 > res) {
        error_status = -res;
        return HAILO_DRIVER_FAIL;
    }
    return HAILO_SUCCESS;
}
#else
#error "Unsupported platform"
#endif

static hailo_status validate_driver_version(const hailo_driver_info &driver_info)
{
    hailo_version_t library_version{};
    auto status = hailo_get_library_version(&library_version);
    CHECK_SUCCESS(status);
    CHECK((driver_info.major_version == library_version.major) &&
        (driver_info.minor_version == library_version.minor) &&
        (driver_info.revision_version == library_version.revision), HAILO_INVALID_DRIVER_VERSION,
        "Driver version ({}.{}.{}) is different from library version ({}.{}.{})",
        driver_info.major_version, driver_info.minor_version, driver_info.revision_version,
        library_version.major, library_version.minor, library_version.revision);
    return HAILO_SUCCESS;
}

HailoRTDriver::HailoRTDriver(const DeviceInfo &device_info, FileDescriptor &&fd, hailo_status &status) :
    m_fd(std::move(fd)),
    m_device_info(device_info),
    m_allocate_driver_buffer(false)
{
    hailo_driver_info driver_info = {};
    int err = 0;
    if (HAILO_SUCCESS != (status = hailo_ioctl(m_fd, HAILO_QUERY_DRIVER_INFO, &driver_info, err))) {
        LOGGER__ERROR("Failed query driver info, errno {}", err);
        return;
    }
    LOGGER__INFO("Hailo PCIe driver version {}.{}.{}", driver_info.major_version,
        driver_info.minor_version, driver_info.revision_version);

    status = validate_driver_version(driver_info);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Driver version mismatch, status {}", status);
        return;
    }

    hailo_device_properties device_properties = {};
    if (HAILO_SUCCESS != (status = hailo_ioctl(m_fd, HAILO_QUERY_DEVICE_PROPERTIES, &device_properties, err))) {
        LOGGER__ERROR("Failed query pcie device properties, errno {}", err);
        return;
    }

    m_desc_max_page_size = device_properties.desc_max_page_size;
    m_allocate_driver_buffer = (HAILO_ALLOCATION_MODE_DRIVER == device_properties.allocation_mode);
    m_dma_engines_count = device_properties.dma_engines_count;

    switch (device_properties.dma_type) {
    case HAILO_DMA_TYPE_PCIE:
        m_dma_type = DmaType::PCIE;
        break;
    case HAILO_DMA_TYPE_DRAM:
        m_dma_type = DmaType::DRAM;
        break;
    default:
        LOGGER__ERROR("Invalid dma type returned from ioctl {}", device_properties.dma_type);
        status = HAILO_DRIVER_FAIL;
        return;
    }

    m_is_fw_loaded = device_properties.is_fw_loaded;

#ifdef __QNX__
    m_resource_manager_pid = device_properties.resource_manager_pid;
#endif // __QNX__

    status = HAILO_SUCCESS;
}

Expected<std::vector<uint8_t>> HailoRTDriver::read_notification()
{
    hailo_d2h_notification notification_buffer = {};

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_READ_NOTIFICATION, &notification_buffer, err);
    if (HAILO_SUCCESS != status) {
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    std::vector<uint8_t> notification(notification_buffer.buffer_len);
    memcpy(notification.data(), notification_buffer.buffer, notification_buffer.buffer_len);
    return notification;
}

hailo_status HailoRTDriver::disable_notifications()
{
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_DISABLE_NOTIFICATION, 0, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HAILO_DISABLE_NOTIFICATION failed with errno: {}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

#if defined(__linux__)
Expected<std::vector<HailoRTDriver::DeviceInfo>> HailoRTDriver::scan_devices()
{
    auto device_names = list_devices();
    CHECK_EXPECTED(device_names, "Failed listing pcie devices");

    std::vector<HailoRTDriver::DeviceInfo> devices_info;
    for (const auto &device_name : device_names.value()) {
        auto device_info = query_device_info(device_name);
        CHECK_EXPECTED(device_info, "failed parsing device info for {}", device_name);
        devices_info.push_back(device_info.release());
    }
    return devices_info;
}
#elif defined(__QNX__)
Expected<std::vector<HailoRTDriver::DeviceInfo>> HailoRTDriver::scan_devices()
{
    auto device_names = list_devices();
    CHECK_EXPECTED(device_names, "Failed listing pcie devices");

    // TODO: HRT-6785 - support multiple devices - currently device_names is vector of one device - in future will be multiple
    std::vector<HailoRTDriver::DeviceInfo> devices_info;
    uint32_t index = 0;
    for (const auto &device_name : device_names.value()) {
        auto device_info = query_device_info(device_name, index);
        CHECK_EXPECTED(device_info, "failed parsing device info for {}", device_name);
        devices_info.push_back(device_info.release());
        index++;
    }
    return devices_info;
}
#else
static_assert(true, "Error, Unsupported Platform");
#endif //defined (__linux__)

Expected<uint32_t> HailoRTDriver::read_vdma_channel_register(vdma::ChannelId channel_id, DmaDirection data_direction,
    size_t offset, size_t  reg_size)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Invalid direction given");
    hailo_vdma_channel_read_register_params params = {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .direction = direction_to_dma_data_direction(data_direction),
        .offset = offset,
        .reg_size = reg_size,
        .data = 0
    };

    int err = 0;
    auto status = hailo_ioctl(m_fd, HAILO_VDMA_CHANNEL_READ_REGISTER, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HailoRTDriver::read_vdma_channel_register failed with errno:{}", err);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    return std::move(params.data);
}

hailo_status HailoRTDriver::write_vdma_channel_register(vdma::ChannelId channel_id, DmaDirection data_direction,
    size_t offset, size_t reg_size, uint32_t data)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Invalid direction given");
    hailo_vdma_channel_write_register_params params = {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .direction = direction_to_dma_data_direction(data_direction),
        .offset = offset,
        .reg_size = reg_size,
        .data = data
    };

    int err = 0;
    auto status = hailo_ioctl(m_fd, HAILO_VDMA_CHANNEL_WRITE_REGISTER, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HailoRTDriver::write_vdma_channel_register failed with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::read_memory(MemoryType memory_type, uint64_t address, void *buf, size_t size)
{
    if (size == 0) {
        LOGGER__ERROR("Invalid size to read");
        return HAILO_INVALID_ARGUMENT;
    }

    if (buf == nullptr) {
        LOGGER__ERROR("Read buffer pointer is NULL");
        return HAILO_INVALID_ARGUMENT;
    }

    constexpr uint32_t CHUNK_SIZE = ARRAY_ENTRIES(hailo_memory_transfer_params::buffer);
    uint32_t offset = 0;

    while (offset < size) {
        const uint32_t actual_size = std::min(CHUNK_SIZE, static_cast<uint32_t>(size) - offset);
        auto status = read_memory_ioctl(memory_type, address + offset,
            reinterpret_cast<uint8_t*>(buf) + offset, actual_size);
        CHECK_SUCCESS(status);
        offset += actual_size;
    }
    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::write_memory(MemoryType memory_type, uint64_t address, const void *buf, size_t size)
{
    if (size == 0) {
        LOGGER__ERROR("Invalid size to read");
        return HAILO_INVALID_ARGUMENT;
    }

    if (buf == nullptr) {
        LOGGER__ERROR("Read buffer pointer is NULL");
        return HAILO_INVALID_ARGUMENT;
    }

    constexpr uint32_t CHUNK_SIZE = ARRAY_ENTRIES(hailo_memory_transfer_params::buffer);
    uint32_t offset = 0;

    while (offset < size) {
        const uint32_t actual_size = std::min(CHUNK_SIZE, static_cast<uint32_t>(size) - offset);
        auto status = write_memory_ioctl(memory_type, address + offset,
            reinterpret_cast<const uint8_t*>(buf) + offset, actual_size);
        CHECK_SUCCESS(status);
        offset += actual_size;
    }
    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::read_memory_ioctl(MemoryType memory_type, uint64_t address, void *buf, size_t size)
{
    hailo_memory_transfer_params transfer = {
        .transfer_direction = TRANSFER_READ,
        .memory_type = translate_memory_type(memory_type),
        .address = address,
        .count = size,
        .buffer = {0}
    };

    if (m_dma_type == DmaType::PCIE) {
        CHECK(address < std::numeric_limits<uint32_t>::max(), HAILO_INVALID_ARGUMENT, "Address out of range {}", address);
    }

    if (size > sizeof(transfer.buffer)) {
        LOGGER__ERROR("Invalid size to read, size given {} is larger than max size {}", size, sizeof(transfer.buffer));
        return HAILO_INVALID_ARGUMENT;
    }

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_MEMORY_TRANSFER, &transfer, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HailoRTDriver::read_memory failed with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }

    memcpy(buf, transfer.buffer, transfer.count);

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::write_memory_ioctl(MemoryType memory_type, uint64_t address, const void *buf, size_t size)
{
    hailo_memory_transfer_params transfer = {
        .transfer_direction = TRANSFER_WRITE,
        .memory_type = translate_memory_type(memory_type),
        .address = address,
        .count = size,
        .buffer = {0}
    };

    if (m_dma_type == DmaType::PCIE) {
        CHECK(address < std::numeric_limits<uint32_t>::max(), HAILO_INVALID_ARGUMENT, "Address out of range {}", address);
    }

    if (size > sizeof(transfer.buffer)) {
        LOGGER__ERROR("Invalid size to read, size given {} is larger than max size {}", size, sizeof(transfer.buffer));
        return HAILO_INVALID_ARGUMENT;
    }

    memcpy(transfer.buffer, buf, transfer.count);

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_MEMORY_TRANSFER, &transfer, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HailoRTDriver::write_memory failed with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_buffer_sync(VdmaBufferHandle handle, DmaSyncDirection sync_direction,
    size_t offset, size_t count)
{
#if defined(__linux__)
    hailo_vdma_buffer_sync_params sync_info{
        .handle = handle,
        .sync_type = (sync_direction == DmaSyncDirection::TO_HOST) ? HAILO_SYNC_FOR_CPU : HAILO_SYNC_FOR_DEVICE,
        .offset = offset,
        .count = count
    };
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_BUFFER_SYNC, &sync_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HAILO_VDMA_BUFFER_SYNC failed with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }
    return HAILO_SUCCESS;
// TODO: HRT-6717 - Remove ifdef when Implement sync ioctl (if determined needed in qnx)
#elif defined( __QNX__)
    (void) handle;
    (void) sync_direction;
    (void) offset;
    (void) count;
    return HAILO_SUCCESS;
#else
#error "unsupported platform!"
#endif // __linux__
}

hailo_status HailoRTDriver::vdma_interrupts_enable(const ChannelsBitmap &channels_bitmap, bool enable_timestamps_measure)
{
    CHECK(is_valid_channels_bitmap(channels_bitmap), HAILO_INVALID_ARGUMENT, "Invalid channel bitmap given");
    hailo_vdma_interrupts_enable_params params{};
    std::copy(channels_bitmap.begin(), channels_bitmap.end(), params.channels_bitmap_per_engine);
    params.enable_timestamps_measure = enable_timestamps_measure;

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_INTERRUPTS_ENABLE, &params, err);
    CHECK_SUCCESS(status, "Failed to enable vdma interrupts with errno:{}", err);

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_interrupts_disable(const ChannelsBitmap &channels_bitmap)
{
    CHECK(is_valid_channels_bitmap(channels_bitmap), HAILO_INVALID_ARGUMENT, "Invalid channel bitmap given");
    hailo_vdma_interrupts_disable_params params{};
    std::copy(channels_bitmap.begin(), channels_bitmap.end(), params.channels_bitmap_per_engine);

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_INTERRUPTS_DISABLE, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to disable vdma interrupts with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

static Expected<IrqData> to_irq_data(const hailo_vdma_interrupts_wait_params& params,
    uint8_t engines_count)
{
    static_assert(ARRAY_ENTRIES(IrqData::channels_irq_data) == ARRAY_ENTRIES(params.irq_data), "Mismatch irq data size");
    CHECK_AS_EXPECTED(params.channels_count <= ARRAY_ENTRIES(params.irq_data), HAILO_DRIVER_FAIL,
        "Invalid channels count returned from vdma_interrupts_wait");

    IrqData irq{};
    irq.channels_count = params.channels_count;
    for (uint8_t i = 0; i < params.channels_count; i++) {
        const auto engine_index = params.irq_data[i].engine_index;
        const auto channel_index = params.irq_data[i].channel_index;
        CHECK_AS_EXPECTED(engine_index < engines_count, HAILO_DRIVER_FAIL,
            "Invalid engine index {} returned from vdma_interrupts_wait, max {}", engine_index, engines_count);
        CHECK_AS_EXPECTED(channel_index < MAX_VDMA_CHANNELS_PER_ENGINE, HAILO_DRIVER_FAIL,
            "Invalid channel_index index {} returned from vdma_interrupts_wait", channel_index);

        irq.channels_irq_data[i].channel_id.engine_index = engine_index;
        irq.channels_irq_data[i].channel_id.channel_index = channel_index;
        irq.channels_irq_data[i].is_active = params.irq_data[i].is_active;
        irq.channels_irq_data[i].desc_num_processed = params.irq_data[i].host_num_processed;
        irq.channels_irq_data[i].host_error = params.irq_data[i].host_error;
        irq.channels_irq_data[i].device_error = params.irq_data[i].device_error;
    }
    return irq;
}

Expected<IrqData> HailoRTDriver::vdma_interrupts_wait(const ChannelsBitmap &channels_bitmap)
{
    CHECK_AS_EXPECTED(is_valid_channels_bitmap(channels_bitmap), HAILO_INVALID_ARGUMENT, "Invalid channel bitmap given");
    hailo_vdma_interrupts_wait_params params{};
    std::copy(channels_bitmap.begin(), channels_bitmap.end(), params.channels_bitmap_per_engine);

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_INTERRUPTS_WAIT, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to wait vdma interrupts with errno:{}", err);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    return to_irq_data(params, static_cast<uint8_t>(m_dma_engines_count));
}

Expected<ChannelInterruptTimestampList> HailoRTDriver::vdma_interrupts_read_timestamps(vdma::ChannelId channel_id)
{
    hailo_vdma_interrupts_read_timestamp_params data{};
    data.engine_index = channel_id.engine_index;
    data.channel_index = channel_id.channel_index;

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_INTERRUPTS_READ_TIMESTAMPS, &data, err);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return create_interrupt_timestamp_list(data);
}

hailo_status HailoRTDriver::fw_control(const void *request, size_t request_len, const uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH],
    void *response, size_t *response_len, uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH],
    std::chrono::milliseconds timeout, hailo_cpu_id_t cpu_id)
{
    CHECK_ARG_NOT_NULL(request);
    CHECK_ARG_NOT_NULL(response);
    CHECK_ARG_NOT_NULL(response_len);
    CHECK(timeout.count() >= 0, HAILO_INVALID_ARGUMENT);

    hailo_fw_control command{};
    static_assert(PCIE_EXPECTED_MD5_LENGTH == sizeof(command.expected_md5), "mismatch md5 size");
    memcpy(&command.expected_md5, request_md5, sizeof(command.expected_md5));
    command.buffer_len = static_cast<uint32_t>(request_len);
    CHECK(request_len <= sizeof(command.buffer), HAILO_INVALID_ARGUMENT,
        "FW control request len can't be larger than {} (size given {})", sizeof(command.buffer), request_len);
    memcpy(&command.buffer, request, request_len);
    command.timeout_ms = static_cast<uint32_t>(timeout.count());
    command.cpu_id = translate_cpu_id(cpu_id);
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_FW_CONTROL, &command, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HAILO_FW_CONTROL failed with errno:{}", err);
        return HAILO_FW_CONTROL_FAILURE;
    }

    if (*response_len < command.buffer_len) {
        LOGGER__ERROR("FW control response len needs to be atleast {} (size given {})", command.buffer_len, *response_len);
        *response_len = command.buffer_len;
        return HAILO_INSUFFICIENT_BUFFER;
    }
    memcpy(response, command.buffer, command.buffer_len);
    *response_len = command.buffer_len;
    memcpy(response_md5, command.expected_md5, PCIE_EXPECTED_MD5_LENGTH);

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::read_log(uint8_t *buffer, size_t buffer_size, size_t *read_bytes, hailo_cpu_id_t cpu_id)
{
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(read_bytes);

    hailo_read_log_params params {
        .cpu_id = translate_cpu_id(cpu_id),
        .buffer = {0},
        .buffer_size = buffer_size,
        .read_bytes = 0
    };

    CHECK(buffer_size <= sizeof(params.buffer), HAILO_DRIVER_FAIL,
        "Given buffer size {} is bigger than buffer size used to read logs {}", buffer_size, sizeof(params.buffer));

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_READ_LOG, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to read log with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }

    CHECK(params.read_bytes <= sizeof(params.buffer), HAILO_DRIVER_FAIL,
        "Amount of bytes read from log {} is bigger than size of buffer {}", params.read_bytes, sizeof(params.buffer));

    memcpy(buffer, params.buffer, params.read_bytes);
    *read_bytes = params.read_bytes;

    return HAILO_SUCCESS;
}
 
hailo_status HailoRTDriver::reset_nn_core()
{
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_RESET_NN_CORE, nullptr, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to reset nn core with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

#if defined(__linux__)
Expected<HailoRTDriver::VdmaBufferHandle> HailoRTDriver::vdma_buffer_map(void *user_address, size_t required_size,
    DmaDirection data_direction, const vdma_mapped_buffer_driver_identifier &driver_buff_handle)
{
    hailo_vdma_buffer_map_params map_user_buffer_info {
        .user_address = user_address,
        .size = required_size,
        .data_direction = direction_to_dma_data_direction(data_direction),
        .allocated_buffer_handle = driver_buff_handle,
        .mapped_handle = 0
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_BUFFER_MAP, &map_user_buffer_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to map user buffer with errno:{}", err);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    return VdmaBufferHandle(map_user_buffer_info.mapped_handle);
}
#elif defined( __QNX__)
Expected<HailoRTDriver::VdmaBufferHandle> HailoRTDriver::vdma_buffer_map(void *user_address, size_t required_size,
    DmaDirection data_direction, const vdma_mapped_buffer_driver_identifier &driver_buff_handle)
{
    // Mapping is done by the driver_buff_handle (shm file descriptor), and not by address.
    (void)user_address;

    // Create shared memory handle to send to driver
    shm_handle_t shm_handle;
    int err = shm_create_handle(driver_buff_handle, m_resource_manager_pid, O_RDWR,
        &shm_handle, 0);
    if (0 != err) {
        LOGGER__ERROR("Error creating shm object handle, errno is: {}", errno);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    hailo_vdma_buffer_map_params map_user_buffer_info {
        .shared_memory_handle = shm_handle,
        .size = required_size,
        .data_direction = direction_to_dma_data_direction(data_direction),
        .allocated_buffer_handle = INVALID_DRIVER_HANDLE_VALUE,
        .mapped_handle = 0
    };

    // Note: The driver will accept the shm_handle, and will mmap it to its own address space. After the driver maps the
    // the shm, calling shm_delete_handle is not needed (but can't harm on the otherhand).
    // If the ioctl fails, we can't tell if the shm was mapped or not, so we delete it ourself.
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_BUFFER_MAP, &map_user_buffer_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to map user buffer with errno:{}", err);
        shm_delete_handle(shm_handle);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    return VdmaBufferHandle(map_user_buffer_info.mapped_handle);
}
#else
#error "unsupported platform!"
#endif // __linux__

hailo_status HailoRTDriver::vdma_buffer_unmap(VdmaBufferHandle handle)
{
    hailo_vdma_buffer_unmap_params unmap_user_buffer_info {
        .mapped_handle = handle
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_BUFFER_UNMAP, &unmap_user_buffer_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to unmap user buffer with errno:{}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<DescriptorsListInfo> HailoRTDriver::descriptors_list_create(size_t desc_count, bool is_circular)
{
    auto handle_to_dma_address_pair = descriptors_list_create_ioctl(desc_count, is_circular);
    CHECK_EXPECTED(handle_to_dma_address_pair);

    const auto desc_handle = handle_to_dma_address_pair->first;
    const auto dma_address = handle_to_dma_address_pair->second;

    auto user_address = descriptors_list_create_mmap(desc_handle, desc_count);
    if (!user_address) {
        auto status = descriptors_list_release_ioctl(desc_handle);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed releasing descriptors list, status {}", status);
            // continue
        }
        return make_unexpected(user_address.status());
    }

    return DescriptorsListInfo{desc_handle, dma_address, desc_count, user_address.release()};
}

hailo_status HailoRTDriver::descriptors_list_release(const DescriptorsListInfo &descriptors_list_info)
{
    hailo_status status = HAILO_SUCCESS;

    auto unmap_status = descriptors_list_create_munmap(descriptors_list_info.user_address, descriptors_list_info.desc_count);
    if (HAILO_SUCCESS != unmap_status) {
        LOGGER__ERROR("Descriptors list unmap failed with {}", unmap_status);
        status = unmap_status;
        // continue
    }

    auto release_status = descriptors_list_release_ioctl(descriptors_list_info.handle);
    if (HAILO_SUCCESS != release_status) {
        LOGGER__ERROR("Descriptors list release status failed with {}", release_status);
        status = release_status;
        // continue
    }

    return status;
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::descriptors_list_create_ioctl(size_t desc_count, bool is_circular)
{
    hailo_desc_list_create_params create_desc_info{};
    create_desc_info.desc_count = desc_count;
    create_desc_info.is_circular = is_circular;

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_DESC_LIST_CREATE, &create_desc_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to create descriptors list with errno:{}", err);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    return std::make_pair(create_desc_info.desc_handle, create_desc_info.dma_address);
}

hailo_status HailoRTDriver::descriptors_list_release_ioctl(uintptr_t desc_handle)
{
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_DESC_LIST_RELEASE, &desc_handle, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to release descriptors list with errno: {}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

#if defined(__linux__)
Expected<void *> HailoRTDriver::descriptors_list_create_mmap(uintptr_t desc_handle, size_t desc_count)
{
    // We lock m_driver_lock before calling mmap. Read m_driver_lock doc in the header
    std::unique_lock<std::mutex> lock(m_driver_lock);

    const size_t buffer_size = desc_count * SIZE_OF_SINGLE_DESCRIPTOR;
    void *address = mmap(nullptr, buffer_size, PROT_WRITE | PROT_READ, MAP_SHARED, m_fd, (off_t)desc_handle);
    if (MAP_FAILED == address) {
        LOGGER__ERROR("Failed to map descriptors list buffer with errno: {}", errno);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }
    return address;
}

hailo_status HailoRTDriver::descriptors_list_create_munmap(void *address, size_t desc_count)
{
    const size_t buffer_size = desc_count * SIZE_OF_SINGLE_DESCRIPTOR;
    if (0 != munmap(address, buffer_size)) {
        LOGGER__ERROR("munmap of address {}, length: {} failed with errno: {}", address, buffer_size, errno);
        return HAILO_DRIVER_FAIL;
    }
    return HAILO_SUCCESS;
}

#elif defined(__QNX__)

Expected<void *> HailoRTDriver::descriptors_list_create_mmap(uintptr_t desc_handle, size_t desc_count)
{
    const size_t buffer_size = desc_count * SIZE_OF_SINGLE_DESCRIPTOR;
    struct hailo_non_linux_desc_list_mmap_params map_vdma_list_params {
        .desc_handle = desc_handle,
        .size = buffer_size,
        .user_address = nullptr,
    };

    int err = 0;
    auto status = HailoRTDriver::hailo_ioctl(m_fd, HAILO_NON_LINUX_DESC_LIST_MMAP, &map_vdma_list_params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Mmap descriptors list ioctl failed with errno:{}", err);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    void *address = mmap(nullptr, buffer_size, PROT_WRITE | PROT_READ | PROT_NOCACHE, MAP_SHARED | MAP_PHYS, NOFD,
        (off_t)map_vdma_list_params.user_address);
    CHECK_AS_EXPECTED(MAP_FAILED != address, HAILO_INTERNAL_FAILURE, "Failed to mmap buffer fd with errno:{}", errno);

    return address;
}

hailo_status HailoRTDriver::descriptors_list_create_munmap(void *address, size_t desc_count)
{
    const size_t buffer_size = desc_count * SIZE_OF_SINGLE_DESCRIPTOR;
    if (0 != munmap(address, buffer_size)) {
        LOGGER__ERROR("munmap of address {}, length: {} failed with errno: {}", address, buffer_size, errno);
        return HAILO_DRIVER_FAIL;
    }
    return HAILO_SUCCESS;
}

#else
#error "unsupported platform!"
#endif

hailo_status HailoRTDriver::descriptors_list_bind_vdma_buffer(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
    uint16_t desc_page_size, uint8_t channel_index, uint32_t starting_desc)
{
    hailo_desc_list_bind_vdma_buffer_params config_info;
    config_info.buffer_handle = buffer_handle;
    config_info.desc_handle = desc_handle;
    config_info.desc_page_size = desc_page_size;
    config_info.channel_index = channel_index;
    config_info.starting_desc = starting_desc;

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_DESC_LIST_BIND_VDMA_BUFFER, &config_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to bind vdma buffer to descriptors list with errno: {}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS; 
}

Expected<uintptr_t> HailoRTDriver::vdma_low_memory_buffer_alloc(size_t size)
{
    CHECK_AS_EXPECTED(m_allocate_driver_buffer, HAILO_INVALID_OPERATION,
        "Tried to allocate buffer from driver even though operation is not supported");

    hailo_allocate_low_memory_buffer_params allocate_params = {
        .buffer_size    = size,
        .buffer_handle  = 0
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_LOW_MEMORY_BUFFER_ALLOC, &allocate_params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to allocate buffer with errno: {}", err);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    return std::move(allocate_params.buffer_handle);
}

hailo_status HailoRTDriver::vdma_low_memory_buffer_free(uintptr_t buffer_handle)
{
    CHECK(m_allocate_driver_buffer, HAILO_INVALID_OPERATION,
        "Tried to free allocated buffer from driver even though operation is not supported");

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_LOW_MEMORY_BUFFER_FREE, (void*)buffer_handle, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to free allocated buffer with errno: {}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS; 
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::vdma_continuous_buffer_alloc(size_t size)
{
    hailo_allocate_continuous_buffer_params params { .buffer_size = size, .buffer_handle = 0, .dma_address = 0 };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CONTINUOUS_BUFFER_ALLOC, &params, err);
    if (HAILO_SUCCESS != status) {
        if (ENOMEM == err) {
            LOGGER__WARN("Failed to allocate continuous buffer, size 0x{:x}. This failure means there is not a sufficient amount of CMA memory",
                size);
            return make_unexpected(HAILO_OUT_OF_HOST_CMA_MEMORY);
        }
        LOGGER__ERROR("Failed allocate continuous buffer with errno:{}", err);
        return make_unexpected(HAILO_DRIVER_FAIL);
    }

    return std::make_pair(params.buffer_handle, params.dma_address);
}

hailo_status HailoRTDriver::vdma_continuous_buffer_free(uintptr_t buffer_handle)
{
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CONTINUOUS_BUFFER_FREE, (void*)buffer_handle, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to free continuous buffer with errno: {}", err);
        return HAILO_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::mark_as_used()
{
    hailo_mark_as_in_use_params params = {
        .in_use = false
    };
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_MARK_AS_IN_USE, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to mark device as in use with errno: {}", err);
        return HAILO_DRIVER_FAIL;
    }
    if (params.in_use) {
        return HAILO_DEVICE_IN_USE;
    }
    return HAILO_SUCCESS;
}

bool HailoRTDriver::is_valid_channel_id(const vdma::ChannelId &channel_id)
{
    return (channel_id.engine_index < m_dma_engines_count) && (channel_id.channel_index < MAX_VDMA_CHANNELS_PER_ENGINE);
}

} /* namespace hailort */
