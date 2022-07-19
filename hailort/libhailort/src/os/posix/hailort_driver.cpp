#include "os/hailort_driver.hpp"
#include "os/posix/pcie_driver_scan.hpp"
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

constexpr hailo_dma_data_direction direction_to_dma_data_direction(HailoRTDriver::DmaDirection direction) {
    switch (direction){
    case HailoRTDriver::DmaDirection::H2D:
        return HAILO_DMA_TO_DEVICE;
    case HailoRTDriver::DmaDirection::D2H:
        return HAILO_DMA_FROM_DEVICE;
    case HailoRTDriver::DmaDirection::BOTH:
        return HAILO_DMA_BIDIRECTIONAL;
    default:
        assert(true);
        // On release build Return value that will make ioctls to fail.
        return HAILO_DMA_NONE;
    }
}

constexpr enum hailo_cpu_id translate_cpu_id(hailo_cpu_id_t cpu_id)
{   
    switch (cpu_id)
    {
    case HAILO_CPU_ID_0:
        return HAILO_CPU_ID_CPU0;
    case HAILO_CPU_ID_1:
        return HAILO_CPU_ID_CPU1;
    default:
        assert(true);
        // On release build Return value that will make ioctls to fail.
        return HAILO_CPU_ID_NONE;
    }
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
    if (0 > ioctl(m_fd, HAILO_QUERY_DRIVER_INFO, &driver_info)) {
        LOGGER__ERROR("Failed query driver info, errno {}", errno);
        status = HAILO_PCIE_DRIVER_FAIL;
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
    if (0 > ioctl(m_fd, HAILO_QUERY_DEVICE_PROPERTIES, &device_properties)) {
        LOGGER__ERROR("Failed query pcie device properties, errno {}", errno);
        status = HAILO_PCIE_DRIVER_FAIL;
        return;
    }

    m_desc_max_page_size = device_properties.desc_max_page_size;
    m_allocate_driver_buffer = (HAILO_ALLOCATION_MODE_DRIVER == device_properties.allocation_mode);
    m_dma_engines_count = device_properties.dma_engines_count;
    switch (device_properties.board_type) {
    case HAILO8:
        m_board_type = BoardType::HAILO8;
        break;
    case HAILO_MERCURY:
        m_board_type = BoardType::MERCURY;
        break;
    default:
        LOGGER__ERROR("Invalid board type returned from ioctl {}", device_properties.board_type);
        status = HAILO_PCIE_DRIVER_FAIL;
        return;
    }

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

#ifdef __QNX__
    m_resource_manager_pid = device_properties.resource_manager_pid;
#endif // __QNX__

    status = HAILO_SUCCESS;
}

Expected<D2H_EVENT_MESSAGE_t> HailoRTDriver::read_notification()
{
    hailo_d2h_notification notification_buffer = {};
    D2H_EVENT_MESSAGE_t notification;

    auto rc = ioctl(this->m_fd, HAILO_READ_NOTIFICATION, &notification_buffer);
    if (0 > rc) {
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    CHECK_AS_EXPECTED(sizeof(notification) >= notification_buffer.buffer_len, HAILO_GET_D2H_EVENT_MESSAGE_FAIL,
        "buffer len is not valid = {}", notification_buffer.buffer_len);

    memcpy(&notification, notification_buffer.buffer, notification_buffer.buffer_len);
    return notification;
}

hailo_status HailoRTDriver::disable_notifications()
{
    auto rc = ioctl(this->m_fd, HAILO_DISABLE_NOTIFICATION, 0);
    CHECK(0 <= rc, HAILO_PCIE_DRIVER_FAIL, "HAILO_DISABLE_NOTIFICATION failed with errno:{}", errno);

    return HAILO_SUCCESS;
}

#if defined(__linux__)
Expected<std::vector<HailoRTDriver::DeviceInfo>> HailoRTDriver::scan_pci()
{
    auto device_names = list_pcie_devices();
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
Expected<std::vector<HailoRTDriver::DeviceInfo>> HailoRTDriver::scan_pci()
{
    auto device_names = list_pcie_devices();
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

Expected<uint32_t> HailoRTDriver::read_vdma_channel_registers(off_t offset, size_t size)
{
    hailo_channel_registers_params params = {
        .transfer_direction = TRANSFER_READ,
        .offset = offset,
        .size = size,
        .data = 0
    };

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_REGISTERS, &params)) {
        LOGGER__ERROR("HailoRTDriver::read_vdma_channel_registers failed with errno:{}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::move(params.data);
}

hailo_status HailoRTDriver::write_vdma_channel_registers(off_t offset, size_t size, uint32_t data)
{
    hailo_channel_registers_params params = {
        .transfer_direction = TRANSFER_WRITE,
        .offset = offset,
        .size = size,
        .data = data
    };

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_REGISTERS, &params)) {
        LOGGER__ERROR("HailoRTDriver::write_vdma_channel_registers failed with errno:{}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::read_bar(PciBar bar, off_t offset, size_t size, void *buf)
{
    if (size == 0) {
        LOGGER__ERROR("Invalid size to read");
        return HAILO_INVALID_ARGUMENT;
    }

    if (buf == nullptr) {
        LOGGER__ERROR("Read buffer pointer is NULL");
        return HAILO_INVALID_ARGUMENT;
    }

    hailo_bar_transfer_params transfer = {
        .transfer_direction = TRANSFER_READ,
        .bar_index = static_cast<uint32_t>(bar),
        .offset = offset,
        .count = size,
        .buffer = {0}
    };

    if (size > sizeof(transfer.buffer)) {
        LOGGER__ERROR("Invalid size to read, size given {} is larger than max size {}", size, sizeof(transfer.buffer));
        return HAILO_INVALID_ARGUMENT;
    }

    if (0 > ioctl(this->m_fd, HAILO_BAR_TRANSFER, &transfer)) {
        LOGGER__ERROR("HailoRTDriver::read_bar failed with errno:{}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    memcpy(buf, transfer.buffer, transfer.count);

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::write_bar(PciBar bar, off_t offset, size_t size, const void *buf)
{
    if (size == 0) {
        LOGGER__ERROR("Invalid size to read");
        return HAILO_INVALID_ARGUMENT;
    }

    if (buf == nullptr) {
        LOGGER__ERROR("Read buffer pointer is NULL");
        return HAILO_INVALID_ARGUMENT;
    }

    hailo_bar_transfer_params transfer = {
        .transfer_direction = TRANSFER_WRITE,
        .bar_index = static_cast<uint32_t>(bar),
        .offset = offset,
        .count = size,
        .buffer = {0}
    };

    if (size > sizeof(transfer.buffer)) {
        LOGGER__ERROR("Invalid size to read, size given {} is larger than max size {}", size, sizeof(transfer.buffer));
        return HAILO_INVALID_ARGUMENT;
    }

    memcpy(transfer.buffer, buf, transfer.count);

    if (0 > ioctl(this->m_fd, HAILO_BAR_TRANSFER, &transfer)) {
        LOGGER__ERROR("HailoRTDriver::write_bar failed with errno:{}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_buffer_sync(VdmaBufferHandle handle, DmaDirection sync_direction, void *address,
    size_t buffer_size)
{
    CHECK(sync_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Can't sync vdma data both host and device");
    hailo_vdma_buffer_sync_params sync_info{
        .handle = handle,
        .sync_type = (sync_direction == DmaDirection::H2D) ? HAILO_SYNC_FOR_DEVICE : HAILO_SYNC_FOR_HOST,
        .buffer_address = address,
        .buffer_size = buffer_size
    };
    if (0 > ioctl(this->m_fd, HAILO_VDMA_BUFFER_SYNC, &sync_info)) {
        LOGGER__ERROR("HAILO_VDMA_BUFFER_SYNC failed with errno:{}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }
    return HAILO_SUCCESS;
}


Expected<HailoRTDriver::VdmaChannelHandle> HailoRTDriver::vdma_channel_enable(vdma::ChannelId channel_id,
    DmaDirection data_direction, uintptr_t desc_list_handle, bool enable_timestamps_measure)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT);
    hailo_vdma_channel_enable_params params {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .direction = direction_to_dma_data_direction(data_direction),
        .desc_list_handle = desc_list_handle,
        .enable_timestamps_measure = enable_timestamps_measure,
        .channel_handle = INVALID_CHANNEL_HANDLE_VALUE,
    };

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_ENABLE, &params)) {
        LOGGER__ERROR("Failed to enable interrupt for channel {} with errno:{}", channel_id, errno);
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

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_DISABLE, &params)) {
        LOGGER__ERROR("Failed to disable interrupt for channel {} with errno:{}", channel_id, errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<ChannelInterruptTimestampList> HailoRTDriver::wait_channel_interrupts(vdma::ChannelId channel_id,
    VdmaChannelHandle channel_handle, const std::chrono::milliseconds &timeout)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(timeout.count() >= 0, HAILO_INVALID_ARGUMENT);

    const uint32_t timestamps_count = MAX_IRQ_TIMESTAMPS_SIZE;
    struct hailo_channel_interrupt_timestamp timestamps[timestamps_count];

    hailo_vdma_channel_wait_params data {
        .engine_index = channel_id.engine_index,
        .channel_index = channel_id.channel_index,
        .channel_handle = channel_handle,
        .timeout_ms = static_cast<uint64_t>(timeout.count()),
        .timestamps = timestamps,
        .timestamps_count = timestamps_count
    };

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_WAIT_INT, &data)) {
        const auto ioctl_errno = errno;
        if (ETIMEDOUT == ioctl_errno) {
            LOGGER__ERROR("Waiting for interrupt for channel {} timed-out (errno=ETIMEDOUT)", channel_id);
            return make_unexpected(HAILO_TIMEOUT);
        }
        if (ECONNABORTED == ioctl_errno) {
            LOGGER__INFO("Channel (index={}) was aborted!", channel_id);
            return make_unexpected(HAILO_STREAM_INTERNAL_ABORT);
        }
        if (ECONNRESET == ioctl_errno) {
            LOGGER__INFO("Channel (index={}) was deactivated!", channel_id);
            return make_unexpected(HAILO_STREAM_NOT_ACTIVATED);
        }
        LOGGER__ERROR("Failed to wait interrupt for channel {} with errno:{}", channel_id, ioctl_errno);
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

    hailo_fw_control command;
    static_assert(PCIE_EXPECTED_MD5_LENGTH == sizeof(command.expected_md5), "mismatch md5 size");
    memcpy(&command.expected_md5, request_md5, sizeof(command.expected_md5));
    command.buffer_len = static_cast<uint32_t>(request_len);
    CHECK(request_len <= sizeof(command.buffer), HAILO_INVALID_ARGUMENT,
        "FW control request len can't be larger than {} (size given {})", sizeof(command.buffer), request_len);
    memcpy(&command.buffer, request, request_len);
    command.timeout_ms = static_cast<uint32_t>(timeout.count());
    command.cpu_id = translate_cpu_id(cpu_id);
    if (0 > ioctl(this->m_fd, HAILO_FW_CONTROL, &command)) {
        LOGGER__ERROR("HAILO_FW_CONTROL failed with errno:{}", errno);
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

    if (0 > ioctl(this->m_fd, HAILO_READ_LOG, &params)) {
        LOGGER__ERROR("Failed to read log with errno:{}", errno);
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
    if (0 > ioctl(this->m_fd, HAILO_RESET_NN_CORE, nullptr)) {
        LOGGER__ERROR("Failed to reset nn core with errno:{}", errno);
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

    if (0 > ioctl(this->m_fd, HAILO_VDMA_BUFFER_MAP, &map_user_buffer_info)) {
        LOGGER__ERROR("Failed to map user buffer with errno:{}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return VdmaBufferHandle(map_user_buffer_info.mapped_handle);
}

hailo_status HailoRTDriver::vdma_buffer_unmap(VdmaBufferHandle handle)
{
    hailo_vdma_buffer_unmap_params unmap_user_buffer_info {
        .mapped_handle = handle
    };

    if (0 > ioctl(this->m_fd, HAILO_VDMA_BUFFER_UNMAP, &unmap_user_buffer_info)) {
        LOGGER__ERROR("Failed to unmap user buffer with errno:{}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::descriptors_list_create(size_t desc_count)
{
    hailo_desc_list_create_params create_desc_info {.desc_count = desc_count, .desc_handle = 0, .dma_address = 0 };

    if (0 > ioctl(this->m_fd, HAILO_DESC_LIST_CREATE, &create_desc_info)) {
        LOGGER__ERROR("Failed to create descriptors list with errno:{}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::make_pair(create_desc_info.desc_handle, create_desc_info.dma_address);
}

hailo_status HailoRTDriver::descriptors_list_release(uintptr_t desc_handle)
{
    if (0 > ioctl(this->m_fd, HAILO_DESC_LIST_RELEASE, &desc_handle)) {
        LOGGER__ERROR("Failed to release descriptors list with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS; 
}

hailo_status HailoRTDriver::descriptors_list_bind_vdma_buffer(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
    uint16_t desc_page_size, uint8_t channel_index)
{
    hailo_desc_list_bind_vdma_buffer_params config_info;
    config_info.buffer_handle = buffer_handle;
    config_info.desc_handle = desc_handle;
    config_info.desc_page_size = desc_page_size;
    config_info.channel_index = channel_index;

    if (0 > ioctl(this->m_fd, HAILO_DESC_LIST_BIND_VDMA_BUFFER, &config_info)) {
        LOGGER__ERROR("Failed to bind vdma buffer to descriptors list with errno: {}", errno);
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

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_ABORT, &params)) {
        const auto ioctl_errno = errno;
        if (ECONNRESET == ioctl_errno) {
            LOGGER__DEBUG("Channel (index={}) was deactivated!", channel_id);
            return HAILO_STREAM_NOT_ACTIVATED;
        }
        else {
            LOGGER__ERROR("Failed to abort vdma channel (index={}) with errno: {}", channel_id, errno);
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

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_CLEAR_ABORT, &params)) {
        const auto ioctl_errno = errno;
        if (ECONNRESET == ioctl_errno) {
            LOGGER__DEBUG("Channel (index={}) was deactivated!", channel_id);
            return HAILO_STREAM_NOT_ACTIVATED;
        }
        else {
            LOGGER__ERROR("Failed to clear abort vdma channel (index={}) with errno: {}", channel_id, errno);
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

    if (0 > ioctl(this->m_fd, HAILO_VDMA_LOW_MEMORY_BUFFER_ALLOC, &allocate_params)) {
        LOGGER__ERROR("Failed to allocate buffer with errno: {}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::move(allocate_params.buffer_handle);
}

hailo_status HailoRTDriver::vdma_low_memory_buffer_free(uintptr_t buffer_handle)
{
    CHECK(m_allocate_driver_buffer, HAILO_INVALID_OPERATION,
        "Tried to free allocated buffer from driver even though operation is not supported");

    if (0 > ioctl(this->m_fd, HAILO_VDMA_LOW_MEMORY_BUFFER_FREE, buffer_handle)) {
        LOGGER__ERROR("Failed to free allocated buffer with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS; 
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::vdma_continuous_buffer_alloc(size_t size)
{
    hailo_allocate_continuous_buffer_params params { .buffer_size = size, .buffer_handle = 0, .dma_address = 0 };

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CONTINUOUS_BUFFER_ALLOC, &params)) {
        LOGGER__ERROR("Failed allocate continuous buffer with errno:{}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::make_pair(params.buffer_handle, params.dma_address);
}

hailo_status HailoRTDriver::vdma_continuous_buffer_free(uintptr_t buffer_handle)
{
    if (0 > ioctl(this->m_fd, HAILO_VDMA_CONTINUOUS_BUFFER_FREE, buffer_handle)) {
        LOGGER__ERROR("Failed to free continuous buffer with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::mark_as_used()
{
    hailo_mark_as_in_use_params params = {
        .in_use = false
    };
    if (0 > ioctl(this->m_fd, HAILO_MARK_AS_IN_USE, &params)) {
        LOGGER__ERROR("Failed to mark device as in use with errno: {}", errno);
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
