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

static Expected<ChannelInterruptTimestampList> create_interrupt_timestamp_list(hailo_vdma_channel_wait_params &inter_data)
{
    CHECK_AS_EXPECTED(inter_data.timestamps_count <= MAX_IRQ_TIMESTAMPS_SIZE, HAILO_PCIE_DRIVER_FAIL,
        "Invalid channel interrupt timestamps count returned {}", inter_data.timestamps_count);
    ChannelInterruptTimestampList timestamp_list;

    timestamp_list.count = inter_data.timestamps_count;
    for (size_t i = 0; i < timestamp_list.count; i++) {
        timestamp_list.timestamp_list[i].timestamp = std::chrono::nanoseconds(inter_data.timestamps[i].timestamp_ns);
        timestamp_list.timestamp_list[i].desc_num_processed = inter_data.timestamps[i].desc_num_processed;
    }
    return timestamp_list;
}

const HailoRTDriver::VdmaChannelHandle HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE = INVALID_CHANNEL_HANDLE_VALUE;
const uintptr_t HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE = INVALID_DRIVER_HANDLE_VALUE;
const uint8_t HailoRTDriver::INVALID_VDMA_CHANNEL_INDEX = INVALID_VDMA_CHANNEL;

Expected<HailoRTDriver> HailoRTDriver::create(const std::string &dev_path)
{
    hailo_status status = HAILO_UNINITIALIZED;

    auto fd = FileDescriptor(open(dev_path.c_str(), O_RDWR));
    if (0 > fd) {
        LOGGER__ERROR("Failed to open board {}", dev_path);
        return make_unexpected(HAILO_OPEN_FILE_FAILURE);
    }

    HailoRTDriver object(dev_path, std::move(fd), status);
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }
    return object;
}

hailo_status HailoRTDriver::hailo_ioctl(int fd, int request, void* request_struct, int &error_status)
{
    int res = ioctl(fd, request, request_struct);
    if (0 > res) {
#if defined(__linux__)
        error_status = errno;
#elif defined(__QNX__)
        error_status = -res;
#else
#error "unsupported platform!"
#endif // __linux__
        switch (error_status) {
            case ETIMEDOUT:
                return HAILO_TIMEOUT;
            case ECONNABORTED:
                return HAILO_STREAM_ABORTED_BY_USER;
            case ECONNRESET:
                return HAILO_STREAM_NOT_ACTIVATED;
            default:
                return HAILO_PCIE_DRIVER_FAIL;
        }
    }
    return HAILO_SUCCESS;
}

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

HailoRTDriver::HailoRTDriver(const std::string &dev_path, FileDescriptor &&fd, hailo_status &status) :
    m_fd(std::move(fd)),
    m_dev_path(dev_path),
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
        status = HAILO_PCIE_DRIVER_FAIL;
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
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
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
        return HAILO_PCIE_DRIVER_FAIL;
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
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
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
        return HAILO_PCIE_DRIVER_FAIL;
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
        return HAILO_PCIE_DRIVER_FAIL;
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
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_buffer_sync(VdmaBufferHandle handle, DmaDirection sync_direction, void *address,
    size_t buffer_size)
{
#if defined(__linux__)
    CHECK(sync_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Can't sync vdma data both host and device");
    hailo_vdma_buffer_sync_params sync_info{
        .handle = handle,
        .sync_type = (sync_direction == DmaDirection::H2D) ? HAILO_SYNC_FOR_DEVICE : HAILO_SYNC_FOR_HOST,
        .buffer_address = address,
        .buffer_size = buffer_size
    };
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_BUFFER_SYNC, &sync_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HAILO_VDMA_BUFFER_SYNC failed with errno:{}", err);
        return HAILO_PCIE_DRIVER_FAIL;
    }
    return HAILO_SUCCESS;
// TODO: HRT-6717 - Remove ifdef when Implement sync ioctl (if determined needed in qnx)
#elif defined( __QNX__)
    (void) handle;
    (void) sync_direction;
    (void) address;
    (void) buffer_size;
    return HAILO_SUCCESS;
#else
#error "unsupported platform!"
#endif // __linux__
}


Expected<HailoRTDriver::VdmaChannelHandle> HailoRTDriver::vdma_channel_enable(vdma::ChannelId channel_id,
    DmaDirection data_direction, bool enable_timestamps_measure)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Invalid direction given");
    hailo_vdma_channel_enable_params params {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .direction = direction_to_dma_data_direction(data_direction),
        .enable_timestamps_measure = enable_timestamps_measure,
        .channel_handle = INVALID_CHANNEL_HANDLE_VALUE,
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CHANNEL_ENABLE, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to enable interrupt for channel {} with errno:{}", channel_id, err);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return VdmaChannelHandle(params.channel_handle);
}

hailo_status HailoRTDriver::vdma_channel_disable(vdma::ChannelId channel_id, VdmaChannelHandle channel_handle)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    hailo_vdma_channel_disable_params params {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .channel_handle = channel_handle
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CHANNEL_DISABLE, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to disable interrupt for channel {} with errno:{}", channel_id, err);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<ChannelInterruptTimestampList> HailoRTDriver::wait_channel_interrupts(vdma::ChannelId channel_id,
    VdmaChannelHandle channel_handle, const std::chrono::milliseconds &timeout)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(timeout.count() >= 0, HAILO_INVALID_ARGUMENT);

#if defined(__linux__)
    struct hailo_channel_interrupt_timestamp timestamps[MAX_IRQ_TIMESTAMPS_SIZE];
#endif

    hailo_vdma_channel_wait_params data {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .channel_handle = channel_handle,
        .timeout_ms = static_cast<uint64_t>(timeout.count()),
        .timestamps_count = MAX_IRQ_TIMESTAMPS_SIZE,
// In linux send address to local buffer because there isnt room on stack for array
#if defined(__linux__)
        .timestamps = timestamps,
#elif defined(__QNX__)
        .timestamps = {}
#else
#error "unsupported platform!"
#endif // __linux__ 
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CHANNEL_WAIT_INT, &data, err);
    if (HAILO_SUCCESS != status) {
        if (HAILO_TIMEOUT == status) {
            LOGGER__ERROR("Waiting for interrupt for channel {} timed-out (errno=ETIMEDOUT)", channel_id);
            return make_unexpected(status);
        }
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            LOGGER__INFO("Channel (index={}) was aborted!", channel_id);
            return make_unexpected(status);
        }
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("Channel (index={}) was deactivated!", channel_id);
            return make_unexpected(status);
        }
        LOGGER__ERROR("Failed to wait interrupt for channel {} with errno:{}", channel_id, err);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

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

    CHECK(buffer_size <= sizeof(params.buffer), HAILO_PCIE_DRIVER_FAIL,
        "Given buffer size {} is bigger than buffer size used to read logs {}", buffer_size, sizeof(params.buffer));

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_READ_LOG, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to read log with errno:{}", err);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    CHECK(params.read_bytes <= sizeof(params.buffer), HAILO_PCIE_DRIVER_FAIL,
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
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}
 
Expected<HailoRTDriver::VdmaBufferHandle> HailoRTDriver::vdma_buffer_map(void *user_address, size_t required_size,
    DmaDirection data_direction, vdma_mapped_buffer_driver_identifier &driver_buff_handle)
{

#if defined(__linux__)
    hailo_vdma_buffer_map_params map_user_buffer_info {
        .user_address = user_address,
        .size = required_size,
        .data_direction = direction_to_dma_data_direction(data_direction),
        .allocated_buffer_handle = driver_buff_handle,
        .mapped_handle = 0
    };
#elif defined( __QNX__)
    hailo_vdma_buffer_map_params map_user_buffer_info {
        .shared_memory_handle = driver_buff_handle.shm_handle,
        .size = required_size,
        .data_direction = direction_to_dma_data_direction(data_direction),
        .allocated_buffer_handle = INVALID_DRIVER_HANDLE_VALUE,
        .mapped_handle = 0
    };

    (void)user_address;
#else
#error "unsupported platform!"
#endif // __linux__

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_BUFFER_MAP, &map_user_buffer_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to map user buffer with errno:{}", err);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return VdmaBufferHandle(map_user_buffer_info.mapped_handle);
}

hailo_status HailoRTDriver::vdma_buffer_unmap(VdmaBufferHandle handle)
{
    hailo_vdma_buffer_unmap_params unmap_user_buffer_info {
        .mapped_handle = handle
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_BUFFER_UNMAP, &unmap_user_buffer_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to unmap user buffer with errno:{}", err);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::descriptors_list_create(size_t desc_count)
{
    hailo_desc_list_create_params create_desc_info {.desc_count = desc_count, .desc_handle = 0, .dma_address = 0 };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_DESC_LIST_CREATE, &create_desc_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to create descriptors list with errno:{}", err);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::make_pair(create_desc_info.desc_handle, create_desc_info.dma_address);
}

hailo_status HailoRTDriver::descriptors_list_release(uintptr_t desc_handle)
{
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_DESC_LIST_RELEASE, &desc_handle, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to release descriptors list with errno: {}", err);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS; 
}

hailo_status HailoRTDriver::descriptors_list_bind_vdma_buffer(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
    uint16_t desc_page_size, uint8_t channel_index, size_t offset)
{
    hailo_desc_list_bind_vdma_buffer_params config_info;
    config_info.buffer_handle = buffer_handle;
    config_info.desc_handle = desc_handle;
    config_info.desc_page_size = desc_page_size;
    config_info.channel_index = channel_index;
    config_info.offset = offset;

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_DESC_LIST_BIND_VDMA_BUFFER, &config_info, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to bind vdma buffer to descriptors list with errno: {}", err);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS; 
}

hailo_status HailoRTDriver::vdma_channel_abort(vdma::ChannelId channel_id, VdmaChannelHandle channel_handle)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);

    hailo_vdma_channel_abort_params params = {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .channel_handle = channel_handle
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CHANNEL_ABORT, &params, err);
    if (HAILO_SUCCESS != status) {
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__DEBUG("Channel (index={}) was deactivated!", channel_id);
            return status;
        }
        else {
            LOGGER__ERROR("Failed to abort vdma channel (index={}) with errno: {}", channel_id, err);
            return HAILO_PCIE_DRIVER_FAIL;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_channel_clear_abort(vdma::ChannelId channel_id, VdmaChannelHandle channel_handle)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);

    hailo_vdma_channel_clear_abort_params params = {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .channel_handle = channel_handle
    };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CHANNEL_CLEAR_ABORT, &params, err);
    if (HAILO_SUCCESS != status) {
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__DEBUG("Channel (index={}) was deactivated!", channel_id);
            return status;
        }
        else {
            LOGGER__ERROR("Failed to clear abort vdma channel (index={}) with errno: {}", channel_id, err);
            return HAILO_PCIE_DRIVER_FAIL;
        }
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
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
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
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS; 
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::vdma_continuous_buffer_alloc(size_t size)
{
    hailo_allocate_continuous_buffer_params params { .buffer_size = size, .buffer_handle = 0, .dma_address = 0 };

    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CONTINUOUS_BUFFER_ALLOC, &params, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed allocate continuous buffer with errno:{}", err);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::make_pair(params.buffer_handle, params.dma_address);
}

hailo_status HailoRTDriver::vdma_continuous_buffer_free(uintptr_t buffer_handle)
{
    int err = 0;
    auto status = hailo_ioctl(this->m_fd, HAILO_VDMA_CONTINUOUS_BUFFER_FREE, (void*)buffer_handle, err);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to free continuous buffer with errno: {}", err);
        return HAILO_PCIE_DRIVER_FAIL;
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
        return HAILO_PCIE_DRIVER_FAIL;
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
