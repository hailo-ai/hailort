/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_driver.cpp
 * @brief Low level interface to PCI driver
 **/

#include "os/windows/osdep.hpp"
#include "os/hailort_driver.hpp"
#include "os/driver_scan.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/os/windows/string_conversion.hpp"
#include "os/mmap_buffer.hpp"
#include "../../../../drivers/win/include/Public.h"

#pragma comment(lib, "cfgmgr32.lib")

namespace hailort
{

static_assert(VDMA_CHANNELS_PER_ENGINE == MAX_VDMA_CHANNELS_PER_ENGINE, "Driver and libhailort parameters mismatch");
static_assert(MIN_D2H_CHANNEL_INDEX == VDMA_DEST_CHANNELS_START, "Driver and libhailort parameters mismatch");

//TODO HRT-7309: merge with posix
static hailo_dma_data_direction direction_to_dma_data_direction(HailoRTDriver::DmaDirection direction) {
    switch (direction){
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
    switch (cpu_id)
    {
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

class CWaitable
{
public:
    ULONG Wait(ULONG millies = INFINITE)
    {
        return WaitForSingleObject(m_Handle, millies);
    }
    ~CWaitable()
    {
         if (m_Handle) {
             CloseHandle(m_Handle);
         }
    }
protected:
    CWaitable(HANDLE h) : m_Handle(h) { }
    HANDLE m_Handle;
};

class CMutex : public CWaitable
{
public:
    CMutex() : CWaitable(CreateMutex(NULL, false, NULL)) { }
    void Release()
    {
        ReleaseMutex(m_Handle);
    }
};

class CEvent : public CWaitable
{
public:
    CEvent(bool Manual) : CWaitable(CreateEvent(NULL, Manual, false, NULL)) { }
};

class COverlapped : public CEvent
{
public:
    COverlapped() : CEvent(true)
    {
        RtlZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
        m_Overlapped.hEvent = m_Handle;
    }
    operator LPOVERLAPPED() { return &m_Overlapped; }
protected:
    OVERLAPPED m_Overlapped;
};

template <typename t>
class CSync
{
public:
    CSync(t& obj) : m_Obj(obj) { m_Obj.Wait(); }
    ~CSync() { m_Obj.Release(); }
private:
    t& m_Obj;
};
using CMutexSync = CSync<CMutex>;

class CDeviceFile
{
public:

    CDeviceFile(const std::string& path)
    {
        Create(path.c_str(), true);
    }
    void Close()
    {
        CMutexSync sync(m_Mutex);
        if (m_Handle) {
            CloseHandle(m_Handle);
            m_Handle = NULL;
        }
    }
    ~CDeviceFile()
    {
        Unregister();
        Close();
    }
    bool Present() const
    {
        return m_Handle;
    }
    HANDLE Detach() {
        CMutexSync sync(m_Mutex);
        HANDLE h = m_Handle;
        m_Handle = NULL;
        return h;
    }
protected:
    bool Notify()
    {
        if (m_Handle) {
            LOGGER__ERROR("Closing the file {}", m_InterfaceName);
        }
        Close();
        return true;
    }
    void Create(LPCSTR Name, bool Writable)
    {
        ULONG access = GENERIC_READ, share = FILE_SHARE_READ;
        if (Writable) {
            access |= GENERIC_WRITE;
        }
        else {
            share |= FILE_SHARE_WRITE;
        }
        m_Handle = CreateFileA(
            Name,
            access,
            share,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL);
        if (m_Handle == INVALID_HANDLE_VALUE) {
            m_Handle = NULL;
            LOGGER__ERROR("can't open '{}'", Name);
            return;
        }

        if (!m_SetNotify) {
            return;
        }

        CM_NOTIFY_FILTER filter;
        filter.cbSize = sizeof(filter);
        filter.Flags = 0;
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
        filter.u.DeviceHandle.hTarget = m_Handle;
        Unregister();
        CM_Register_Notification(&filter, this, [](
            _In_ HCMNOTIFICATION,
            _In_opt_ PVOID             Context,
            _In_ CM_NOTIFY_ACTION      Action,
            _In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA,
            _In_ DWORD) -> DWORD
            {
                CDeviceFile* f = (CDeviceFile*)Context;
                if (Action == CM_NOTIFY_ACTION_DEVICEQUERYREMOVE) {
                    return f->Notify() ? ERROR_SUCCESS : ERROR_CANCELLED;
                }
                if (Action == CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE) {
                    f->Notify();
                }
                return ERROR_SUCCESS;
            },
            &m_Notification);
    }
    void Unregister()
    {
        if (m_Notification) {
            CM_Unregister_Notification(m_Notification);
            m_Notification = NULL;
        }
    }
private:
    std::string m_InterfaceName;
    HCMNOTIFICATION m_Notification = NULL;
    CMutex m_Mutex;
    bool m_SetNotify = false;
    HANDLE m_Handle = NULL;
};

// TODO: HRT-7309 : implement hailo_ioctl for windows
static int ioctl(HANDLE h, ULONG val, tCompatibleHailoIoctlData *ioctl_data)
{
    ioctl_data->Parameters.u.value = val;
    ULONG returned;
    COverlapped overlapped;
    bool res = DeviceIoControl(h, HAILO_IOCTL_COMPATIBLE, ioctl_data, sizeof(*ioctl_data),
                               ioctl_data, sizeof(*ioctl_data), &returned, overlapped);
    if (!res) {
        ULONG lastError = GetLastError();
        if (lastError != ERROR_IO_PENDING) {
            errno = (int)lastError;
            return -1;
        }
        if (!GetOverlappedResult(h, overlapped, &returned, true)) {
            errno = (int)GetLastError();
            return -1;
        }
    }
    return 0;
}

const HailoRTDriver::VdmaChannelHandle HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE = INVALID_CHANNEL_HANDLE_VALUE;
const uintptr_t HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE = INVALID_DRIVER_HANDLE_VALUE;
const uint8_t HailoRTDriver::INVALID_VDMA_CHANNEL_INDEX = INVALID_VDMA_CHANNEL;

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
    tCompatibleHailoIoctlData data = {};
    hailo_driver_info& driver_info = data.Buffer.DriverInfo;
    if (0 > ioctl(m_fd, HAILO_QUERY_DRIVER_INFO, &data)) {
        LOGGER__ERROR("Failed to query driver info, errno {}", errno);
        status = HAILO_PCIE_DRIVER_FAIL;
        return;
    }
    status = validate_driver_version(driver_info);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Driver version mismatch, status {}", status);
        return;
    }

    hailo_device_properties& device_properties = data.Buffer.DeviceProperties;
    if (0 > ioctl(m_fd, HAILO_QUERY_DEVICE_PROPERTIES, &data)) {
        LOGGER__ERROR("Failed query pcie device properties, errno {}", errno);
        status = HAILO_PCIE_DRIVER_FAIL;
        return;
    }

    m_desc_max_page_size = device_properties.desc_max_page_size;
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
    status = HAILO_SUCCESS;
}

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

Expected<HailoRTDriver> HailoRTDriver::create(const std::string &dev_path)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CDeviceFile f(dev_path);
    if (!f.Present()) {
        LOGGER__ERROR("Failed to open board {}", dev_path);
        return make_unexpected(HAILO_OPEN_FILE_FAILURE);
    }
    FileDescriptor fd(f.Detach());

    HailoRTDriver platform(dev_path, std::move(fd), status);
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }
    return platform;
}

Expected<std::vector<uint8_t>> HailoRTDriver::read_notification()
{
    tCompatibleHailoIoctlData data;
    hailo_d2h_notification& notification_buffer = data.Buffer.D2HNotification;

    auto rc = ioctl(this->m_fd, HAILO_READ_NOTIFICATION, &data);
    if (0 > rc) {
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    std::vector<uint8_t> notification(notification_buffer.buffer_len);
    memcpy(notification.data(), notification_buffer.buffer, notification_buffer.buffer_len);
    return notification;
}

hailo_status HailoRTDriver::disable_notifications()
{
    tCompatibleHailoIoctlData data = {};

    int res = ioctl(m_fd, HAILO_DISABLE_NOTIFICATION, &data);
    CHECK(0 <= res, HAILO_PCIE_DRIVER_FAIL, "HAILO_DISABLE_NOTIFICATION failed with errno: {}", errno);

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
    if (size == 0) {
        LOGGER__ERROR("Invalid size to read");
        return HAILO_INVALID_ARGUMENT;
    }

    if (buf == nullptr) {
        LOGGER__ERROR("Read buffer pointer is NULL");
        return HAILO_INVALID_ARGUMENT;
    }

    if (m_dma_type == DmaType::PCIE) {
        CHECK(address < std::numeric_limits<uint32_t>::max(), HAILO_INVALID_ARGUMENT, "Address out of range {}", address);
    }

    tCompatibleHailoIoctlData data = {};
    hailo_memory_transfer_params& transfer = data.Buffer.MemoryTransfer;
    transfer.transfer_direction = TRANSFER_READ;
    transfer.memory_type = translate_memory_type(memory_type);
    transfer.address = address;
    transfer.count = size;
    memset(transfer.buffer, 0, sizeof(transfer.buffer));

    CHECK(size <= sizeof(transfer.buffer), HAILO_INVALID_ARGUMENT,
        "Invalid size to read, size given {} is larger than max size {}", size, sizeof(transfer.buffer));

    if (0 > ioctl(m_fd, HAILO_MEMORY_TRANSFER, &data)) {
        LOGGER__ERROR("HailoRTDriver::read_memory failed with errno:{}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    memcpy(buf, transfer.buffer, transfer.count);

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::write_memory_ioctl(MemoryType memory_type, uint64_t address, const void *buf, size_t size)
{
    if (size == 0) {
        LOGGER__ERROR("Invalid size to write");
        return HAILO_INVALID_ARGUMENT;
    }

    if (buf == nullptr) {
        LOGGER__ERROR("Write buffer pointer is NULL");
        return HAILO_INVALID_ARGUMENT;
    }

    if (m_dma_type == DmaType::PCIE) {
        CHECK(address < std::numeric_limits<uint32_t>::max(), HAILO_INVALID_ARGUMENT, "Address out of range {}", address);
    }

    tCompatibleHailoIoctlData data = {};
    hailo_memory_transfer_params& transfer = data.Buffer.MemoryTransfer;
    transfer.transfer_direction = TRANSFER_WRITE;
    transfer.memory_type = translate_memory_type(memory_type);
    transfer.address = address;
    transfer.count = size;
    memset(transfer.buffer, 0, sizeof(transfer.buffer));

    CHECK(size <= sizeof(transfer.buffer), HAILO_INVALID_ARGUMENT,
        "Invalid size to write, size given {} is larger than max size {}", size, sizeof(transfer.buffer));

    memcpy(transfer.buffer, buf, transfer.count);

    if (0 > ioctl(this->m_fd, HAILO_MEMORY_TRANSFER, &data)) {
        LOGGER__ERROR("HailoRTDriver::write_memory failed with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<uint32_t> HailoRTDriver::read_vdma_channel_register(vdma::ChannelId channel_id, DmaDirection data_direction,
    size_t offset, size_t reg_size)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Invalid direction given");

    tCompatibleHailoIoctlData data = {};
    auto& params = data.Buffer.ChannelRegisterRead;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.direction = direction_to_dma_data_direction(data_direction);
    params.offset = offset;
    params.reg_size = reg_size;
    params.data = 0;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_READ_REGISTER, &data)) {
        LOGGER__ERROR("HailoRTDriver::read_vdma_channel_register failed with errno: {}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::move(params.data);
}

hailo_status HailoRTDriver::write_vdma_channel_register(vdma::ChannelId channel_id, DmaDirection data_direction,
    size_t offset, size_t reg_size, uint32_t value)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Invalid direction given");

    tCompatibleHailoIoctlData data = {};
    auto& params = data.Buffer.ChannelRegisterWrite;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.direction = direction_to_dma_data_direction(data_direction);
    params.offset = offset;
    params.reg_size = reg_size;
    params.data = value;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_WRITE_REGISTER, &data)) {
        LOGGER__ERROR("HailoRTDriver::write_vdma_channel_register failed with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_buffer_sync(VdmaBufferHandle handle, DmaDirection sync_direction, void *address,
    size_t buffer_size)
{
    CHECK(sync_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Can't sync vdma data both host and device");
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_buffer_sync_params& sync_info = data.Buffer.VdmaBufferSync;
    sync_info.handle = handle;
    sync_info.sync_type = (sync_direction == DmaDirection::H2D) ? HAILO_SYNC_FOR_DEVICE : HAILO_SYNC_FOR_HOST;
    sync_info.buffer_address = address;
    sync_info.buffer_size = buffer_size;
    if (0 > ioctl(this->m_fd, HAILO_VDMA_BUFFER_SYNC, &data)) {
        LOGGER__ERROR("HAILO_VDMA_BUFFER_SYNC failed with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }
    return HAILO_SUCCESS;
}

Expected<HailoRTDriver::VdmaChannelHandle> HailoRTDriver::vdma_channel_enable(vdma::ChannelId channel_id,
    DmaDirection data_direction, bool enable_timestamps_measure)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT, "Invalid direction given");
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_channel_enable_params& params = data.Buffer.ChannelEnable;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.direction = direction_to_dma_data_direction(data_direction);
    params.enable_timestamps_measure = enable_timestamps_measure;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_ENABLE, &data)) {
        LOGGER__ERROR("Failed to enable interrupt for channel {} with errno: {}", channel_id.channel_index, errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::move(params.channel_handle);
}

hailo_status HailoRTDriver::vdma_channel_disable(vdma::ChannelId channel_id, VdmaChannelHandle channel_handle)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_channel_disable_params& params = data.Buffer.ChannelDisable;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.channel_handle = channel_handle;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_DISABLE, &data)) {
        LOGGER__ERROR("Failed to disable interrupt for channel {} with errno: {}", channel_id, errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

//TODO: unify
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
    return std::move(timestamp_list);
}

Expected<ChannelInterruptTimestampList> HailoRTDriver::wait_channel_interrupts(vdma::ChannelId channel_id,
    VdmaChannelHandle channel_handle, const std::chrono::milliseconds &timeout)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(timeout.count() >= 0, HAILO_INVALID_ARGUMENT);

    tCompatibleHailoIoctlData data = {};
    hailo_vdma_channel_wait_params& params = data.Buffer.ChannelWait;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.channel_handle = channel_handle;
    params.timeout_ms = static_cast<uint64_t>(timeout.count());
    params.timestamps_count = MAX_IRQ_TIMESTAMPS_SIZE;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_WAIT_INT, &data)) {
        const auto ioctl_errno = errno;
        if (ERROR_SEM_TIMEOUT == ioctl_errno) {
            LOGGER__ERROR("Waiting for interrupt for channel {} timed-out", channel_id);
            return make_unexpected(HAILO_TIMEOUT);
        }
        if (ERROR_OPERATION_ABORTED == ioctl_errno) {
            LOGGER__INFO("Stream (index={}) was aborted!", channel_id);
            return make_unexpected(HAILO_STREAM_ABORTED_BY_USER);
        }
        if (ERROR_NOT_READY == ioctl_errno) {
            LOGGER__INFO("Channel (index={}) was deactivated!", channel_id);
            return make_unexpected(HAILO_STREAM_NOT_ACTIVATED);
        }
        LOGGER__ERROR("Failed to wait interrupt for channel {} with errno: {}", channel_id, ioctl_errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return create_interrupt_timestamp_list(params);
}

hailo_status HailoRTDriver::fw_control(const void *request, size_t request_len, const uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH],
    void *response, size_t *response_len, uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH],
    std::chrono::milliseconds timeout, hailo_cpu_id_t cpu_id)
{
    CHECK_ARG_NOT_NULL(request);
    CHECK_ARG_NOT_NULL(response);
    CHECK_ARG_NOT_NULL(response_len);
    CHECK(timeout.count() >= 0, HAILO_INVALID_ARGUMENT);

    tCompatibleHailoIoctlData data = {};
    hailo_fw_control& command = data.Buffer.FirmwareControl;
    static_assert(PCIE_EXPECTED_MD5_LENGTH == sizeof(command.expected_md5), "mismatch md5 size");
    memcpy(&command.expected_md5, request_md5, sizeof(command.expected_md5));
    command.buffer_len = static_cast<uint32_t>(request_len);
    CHECK(request_len <= sizeof(command.buffer), HAILO_INVALID_ARGUMENT,
        "FW control request len can't be larger than {} (size given {})", sizeof(command.buffer), request_len);
    memcpy(&command.buffer, request, request_len);
    command.timeout_ms = static_cast<uint32_t>(timeout.count());
    command.cpu_id = translate_cpu_id(cpu_id);

    if (0 > ioctl(this->m_fd, HAILO_FW_CONTROL, &data)) {
        LOGGER__ERROR("HAILO_FW_CONTROL failed with errno: {}", errno);
        return HAILO_FW_CONTROL_FAILURE;
    }

    if (*response_len < command.buffer_len) {
        LOGGER__ERROR("FW control response len needs to be at least {} (size given {})", command.buffer_len, *response_len);
        *response_len = command.buffer_len;
        return HAILO_INSUFFICIENT_BUFFER;
    }
    memcpy(response, command.buffer, command.buffer_len);
    *response_len = command.buffer_len;
    memcpy(response_md5, command.expected_md5, PCIE_EXPECTED_MD5_LENGTH);

    return HAILO_SUCCESS;
}

hailo_status read_log(uint8_t *buffer, size_t buffer_size, size_t *read_bytes, hailo_cpu_id_t cpu_id)
{
    (void)buffer;
    (void)buffer_size;
    (void)read_bytes;
    (void)cpu_id;
    return HAILO_PCIE_NOT_SUPPORTED_ON_PLATFORM;
}

Expected<size_t> HailoRTDriver::vdma_buffer_map(void *user_address, size_t required_size, DmaDirection data_direction,
    vdma_mapped_buffer_driver_identifier &driver_buff_handle)
{
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_buffer_map_params& map_user_buffer_info = data.Buffer.VdmaBufferMap;
    map_user_buffer_info.user_address = user_address;
    map_user_buffer_info.size = required_size;
    map_user_buffer_info.data_direction = direction_to_dma_data_direction(data_direction);
    map_user_buffer_info.allocated_buffer_handle = driver_buff_handle;
    map_user_buffer_info.mapped_handle = 0;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_BUFFER_MAP, &data)) {
        LOGGER__ERROR("Failed to map user buffer with errno: {}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::move(map_user_buffer_info.mapped_handle);
}

hailo_status HailoRTDriver::vdma_buffer_unmap(VdmaBufferHandle handle)
{
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_buffer_unmap_params& unmap_user_buffer_info = data.Buffer.VdmaBufferUnmap;
    unmap_user_buffer_info.mapped_handle = handle;
    if (0 > ioctl(this->m_fd, HAILO_VDMA_BUFFER_UNMAP, &data)) {
        LOGGER__ERROR("Failed to unmap user buffer with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::descriptors_list_create(size_t desc_count)
{
    tCompatibleHailoIoctlData data = {};
    hailo_desc_list_create_params& create_desc_info = data.Buffer.DescListCreate;
    create_desc_info.desc_count = desc_count;
    create_desc_info.desc_handle = 0;
    create_desc_info.dma_address = 0;

    if (0 > ioctl(this->m_fd, HAILO_DESC_LIST_CREATE, &data)) {
        LOGGER__ERROR("Failed to create descriptors list with errno: {}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::move(std::make_pair(create_desc_info.desc_handle, create_desc_info.dma_address));
}

hailo_status HailoRTDriver::descriptors_list_release(uintptr_t desc_handle)
{
    tCompatibleHailoIoctlData data = {};
    uintptr_t& release_desc_info = data.Buffer.DescListReleaseParam; 
    release_desc_info = desc_handle;
    if (0 > ioctl(this->m_fd, HAILO_DESC_LIST_RELEASE, &data)) {
        LOGGER__ERROR("Failed to release descriptors list with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::descriptors_list_bind_vdma_buffer(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
    uint16_t desc_page_size,  uint8_t channel_index, size_t offset)
{
    tCompatibleHailoIoctlData data = {};
    hailo_desc_list_bind_vdma_buffer_params& config_info = data.Buffer.DescListBind;
    config_info.buffer_handle = buffer_handle;
    config_info.desc_handle = desc_handle;
    config_info.desc_page_size = desc_page_size;
    config_info.channel_index = channel_index;
    config_info.offset = offset;

    if (0 > ioctl(this->m_fd, HAILO_DESC_LIST_BIND_VDMA_BUFFER, &data)) {
        LOGGER__ERROR("Failed to bind vdma buffer to descriptors list with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_channel_abort(vdma::ChannelId channel_id, VdmaChannelHandle channel_handle)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_channel_abort_params& params = data.Buffer.ChannelAbort;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.channel_handle = channel_handle;
    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_ABORT, &data)) {
        LOGGER__ERROR("Failed to abort vdma channel (index={}) with errno: {}", channel_id, errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_channel_clear_abort(vdma::ChannelId channel_id, VdmaChannelHandle channel_handle)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_channel_clear_abort_params& params = data.Buffer.ChannelClearAbort;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.channel_handle = channel_handle;
    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_CLEAR_ABORT, &data)) {
        LOGGER__ERROR("Failed to clear abort vdma channel (index={}) with errno: {}", channel_id, errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::read_log(uint8_t *buffer, size_t buffer_size, size_t *read_bytes, hailo_cpu_id_t cpu_id)
{
    tCompatibleHailoIoctlData data = {};
    hailo_read_log_params& params = data.Buffer.ReadLog;
    params.buffer_size = __min(buffer_size, sizeof(params.buffer));
    params.cpu_id = translate_cpu_id(cpu_id);

    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(read_bytes);

    if (0 > ioctl(this->m_fd, HAILO_READ_LOG, &data)) {
      LOGGER__ERROR("Failed to read log with errno:{}", errno);
      return HAILO_PCIE_DRIVER_FAIL;
    }

    CHECK(params.read_bytes <= sizeof(params.buffer), HAILO_PCIE_DRIVER_FAIL,
        "Amount of bytes read from log {} is bigger than size of buffer {}",
        params.read_bytes, sizeof(params.buffer));

    memcpy(buffer, params.buffer, params.read_bytes);
    *read_bytes = params.read_bytes;

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::reset_nn_core()
{
    LOGGER__ERROR("Reset nn core is not supported over the windows driver");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<MmapBufferImpl> MmapBufferImpl::create_file_map(size_t length, FileDescriptor &file, uintptr_t offset)
{
    tCompatibleHailoIoctlData data = {};
    data.Buffer.DescListMmap.desc_handle = offset;
    data.Buffer.DescListMmap.size = length;
    if (0 > ioctl(file, HAILO_NON_LINUX_DESC_LIST_MMAP, &data)) {
        LOGGER__ERROR("Failed to map physical memory with errno: {}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }
    // this mapping will be deleted automatically with the physical allocation
    return MmapBufferImpl(data.Buffer.DescListMmap.user_address, length, false);
}

Expected<uintptr_t> HailoRTDriver::vdma_low_memory_buffer_alloc(size_t size) {
    (void) size;
    return make_unexpected(HAILO_INVALID_OPERATION);
}


hailo_status HailoRTDriver::vdma_low_memory_buffer_free(uintptr_t buffer_handle) {
    (void) buffer_handle;
    return HAILO_INVALID_OPERATION;
}

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::vdma_continuous_buffer_alloc(size_t size)
{
    (void) size;
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status HailoRTDriver::vdma_continuous_buffer_free(uintptr_t buffer_handle)
{
    (void) buffer_handle;
    return HAILO_INVALID_OPERATION;
}

hailo_status HailoRTDriver::mark_as_used()
{
    tCompatibleHailoIoctlData data = {};
    if (0 > ioctl(this->m_fd, HAILO_MARK_AS_IN_USE, &data)) {
        LOGGER__ERROR("Failed to mark device as in use with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }
    if (data.Buffer.MarkAsInUse.in_use) {
        return HAILO_DEVICE_IN_USE;
    }
    return HAILO_SUCCESS;
}

// TODO: HRT-7309 merge with posix
bool HailoRTDriver::is_valid_channel_id(const vdma::ChannelId &channel_id)
{
    return (channel_id.engine_index < m_dma_engines_count) && (channel_id.channel_index < MAX_VDMA_CHANNELS_PER_ENGINE);
}

} /* namespace hailort */
