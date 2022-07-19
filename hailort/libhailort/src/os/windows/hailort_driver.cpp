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

class CDeviceProperty
{
public:
    CDeviceProperty(LPCSTR FriendlyName) : m_FriendlyName(FriendlyName) {}
    ~CDeviceProperty()
    {
        Drop();
    }
    bool IsValid() const
    {
        return m_Buffer != NULL;
    }
    void MoveTo(CDeviceProperty& other)
    {
        other.Drop();
        other.m_Size = m_Size;
        other.m_Buffer = m_Buffer;
        other.m_String = m_String;
        other.m_Type = m_Type;
        other.m_Value = m_Value;
        m_Buffer = NULL;
    }
    bool Number(uint32_t& Value) const {
        Value = m_Value;
        return m_Type == DEVPROP_TYPE_UINT32 && IsValid();
    }
protected:
    PBYTE m_Buffer = NULL;
    ULONG m_Size = 0;
    DEVPROPTYPE m_Type = DEVPROP_TYPE_EMPTY;
    std::wstring m_String;
    LPCSTR m_FriendlyName;
    uint32_t m_Value = 0;
protected:
    void Drop()
    {
        if (m_Buffer) free(m_Buffer);
        m_Buffer = NULL;
        m_Size = 0;
        m_Type = DEVPROP_TYPE_EMPTY;
    }
    void PostProcess(CONFIGRET cr)
    {
        if (cr != CR_SUCCESS) {
            Drop();
        }
        if (m_Type == DEVPROP_TYPE_STRING) {
            m_String = (wchar_t *)m_Buffer;
        }
        if (m_Type == DEVPROP_TYPE_UINT32) {
            if (m_Size == sizeof(uint32_t)) {
                m_Value = *(uint32_t *)m_Buffer;
            } else {
                Drop();
            }
        }
    }
};

class CDeviceInterfaceProperty : public CDeviceProperty
{
public:
    CDeviceInterfaceProperty(LPCWSTR DevInterface, const DEVPROPKEY* Key, LPCSTR FriendlyName, bool AllowRecursion = true);
};

class CDevInstProperty : public CDeviceProperty
{
public:
    CDevInstProperty(LPCWSTR DevInst, const DEVPROPKEY* Key, LPCSTR FriendlyName) :
        CDeviceProperty(FriendlyName)
    {
        DEVINST dn;
        CONFIGRET cr = CM_Locate_DevNodeW(&dn, (WCHAR *)DevInst, CM_LOCATE_DEVNODE_NORMAL);
        if (cr != CR_SUCCESS)
            return;
        // try to get the size of the property
        CM_Get_DevNode_PropertyW(dn, Key, &m_Type, NULL, &m_Size, 0);
        if (!m_Size)
            return;
        m_Buffer = (PBYTE)malloc(m_Size);
        if (!m_Buffer) {
            return;
        }
        cr = CM_Get_DevNode_PropertyW(dn, Key, &m_Type, m_Buffer, &m_Size, 0);
        PostProcess(cr);
    }
};

class CDeviceInstancePropertyOfInterface : public CDeviceInterfaceProperty
{
public:
    CDeviceInstancePropertyOfInterface(LPCWSTR DevInterface) :
        CDeviceInterfaceProperty(DevInterface, &DEVPKEY_Device_InstanceId, "DevInstance", false)
    { }
    const std::wstring& DevInst() const { return m_String; }
};

bool IsSame(const HailoRTDriver::DeviceInfo& a, const HailoRTDriver::DeviceInfo& b)
{
    return a.bus == b.bus && a.device == b.device && a.func == b.func;
}

bool IsAny(const HailoRTDriver::DeviceInfo& a)
{
    return a.bus == MAXUINT;
}

class CDevicePCILocation
{
public:
    CDevicePCILocation(LPCWSTR DevInterface)
    {
        CDeviceInterfaceProperty BusNumber(DevInterface, &DEVPKEY_Device_BusNumber, "BusNumber");
        CDeviceInterfaceProperty Address(DevInterface, &DEVPKEY_Device_Address, "Address");
        m_Valid = BusNumber.Number(m_Location.bus) && Address.Number(m_Location.device);
        if (m_Valid) {
            m_Location.func = m_Location.device & 0xff;
            m_Location.device = m_Location.device >> 16;
        }
        std::wstring devInterface = DevInterface;
        m_Location.dev_path = StringConverter::utf16_to_ansi(devInterface).value();
    }
    const HailoRTDriver::DeviceInfo& Location() const { return m_Location; }
    bool IsValid() const { return m_Valid; }
private:
    HailoRTDriver::DeviceInfo m_Location = {};
    bool m_Valid = false;
};

CDeviceInterfaceProperty::CDeviceInterfaceProperty(
    LPCWSTR DevInterface,
    const DEVPROPKEY* Key,
    LPCSTR FriendlyName,
    bool AllowRecursion) : CDeviceProperty(FriendlyName)
{
    // try to get the property via device interface
    CM_Get_Device_Interface_PropertyW(DevInterface, Key, &m_Type, NULL, &m_Size, 0);
    if (!m_Size) {
        if (AllowRecursion) {
            // try to get the property via device instance
            CDeviceInstancePropertyOfInterface diProp(DevInterface);
            if (diProp.IsValid()) {
                const std::wstring& di = diProp.DevInst();
                CDevInstProperty dip(di.c_str(), Key, FriendlyName);
                if (dip.IsValid()) {
                    dip.MoveTo(*this);
                }
            }
        }
        return;
    }
    m_Buffer = (PBYTE)malloc(m_Size);
    if (!m_Buffer)
        return;
    CONFIGRET cr = CM_Get_Device_Interface_PropertyW(
        DevInterface, Key, &m_Type, m_Buffer, &m_Size, 0);
    PostProcess(cr);
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
    CDeviceFile(std::vector<HailoRTDriver::DeviceInfo>& Instances)
    {
        EnumerateInstances(Instances);
    }
    CDeviceFile(const std::string& path)
    {
        std::vector<HailoRTDriver::DeviceInfo> found;
        EnumerateInstances(found);
        for (size_t i = 0; i < found.size(); ++i) {
            if (path == found[i].dev_path) {
                Create(path.c_str(), true);
                break;
            }
        }
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
    void EnumerateInstances(std::vector<HailoRTDriver::DeviceInfo>& Instances)
    {
        CONFIGRET cr;
        WCHAR* names = NULL, * currentName;
        ULONG len = 0;
        do {
            cr = CM_Get_Device_Interface_List_SizeW(
                &len,
                &m_InterfaceGuid,
                NULL,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

            if (cr != CR_SUCCESS || len < 2) {
                LOGGER__ERROR("Driver interface not found, error {}", cr);
                break;
            }
            if (len <= 1) {
                LOGGER__ERROR("Driver interface not found");
                break;
            }
            names = (WCHAR*)malloc(len * sizeof(WCHAR));
            if (!names) {
                LOGGER__ERROR("Can't allocate buffer of {} chars", len);
                cr = CR_OUT_OF_MEMORY;
                break;
            }
            cr = CM_Get_Device_Interface_ListW(
                &m_InterfaceGuid,
                NULL,
                names,
                len,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
            if (cr != CR_SUCCESS) {
                LOGGER__ERROR("Can't retrieve driver interface, error {}", cr);
                break;
            }
            for (currentName = names; names && *currentName; currentName += wcslen(currentName) + 1) {
                CDevicePCILocation locationData(currentName);
                if (!locationData.IsValid())
                    continue;
                Instances.push_back(locationData.Location());
            }
        } while (false);

        if (names)
        {
            free(names);
        }
    }
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
    GUID m_InterfaceGuid = GUID_DEVINTERFACE_HailoKM;
    std::string m_InterfaceName;
    HCMNOTIFICATION m_Notification = NULL;
    CMutex m_Mutex;
    bool m_SetNotify = false;
    HANDLE m_Handle = NULL;
};


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
    switch (device_properties.board_type) {
    case HAILO8:
        m_board_type = BoardType::HAILO8;
        break;
    case HAILO_MERCURY:
        m_board_type = BoardType::MERCURY;
        break;
    default:
        LOGGER__ERROR("Invalid board type {} returned from ioctl", device_properties.board_type);
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

    status = HAILO_SUCCESS;
}

Expected<std::vector<HailoRTDriver::DeviceInfo>> HailoRTDriver::scan_pci()
{
    std::vector<HailoRTDriver::DeviceInfo> all;
    CDeviceFile f(all);
    for (size_t i = 0; i < all.size(); ++i) {
        const HailoRTDriver::DeviceInfo& di = all[i];
        LOGGER__INFO("Found {}:{}:{} {}", di.bus, di.device, di.func, di.dev_path);
    }
    return std::move(all);
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

Expected<D2H_EVENT_MESSAGE_t> HailoRTDriver::read_notification()
{
    tCompatibleHailoIoctlData data;
    D2H_EVENT_MESSAGE_t notification;
    hailo_d2h_notification& notification_buffer = data.Buffer.D2HNotification;

    auto rc = ioctl(this->m_fd, HAILO_READ_NOTIFICATION, &data);
    if (0 > rc) {
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    CHECK_AS_EXPECTED(sizeof(notification) >= notification_buffer.buffer_len, HAILO_GET_D2H_EVENT_MESSAGE_FAIL,
        "buffer len is not valid = {}", notification_buffer.buffer_len);

    memcpy(&notification, notification_buffer.buffer, notification_buffer.buffer_len);
    return std::move(notification);
}

hailo_status HailoRTDriver::disable_notifications()
{
    tCompatibleHailoIoctlData data = {};

    int res = ioctl(m_fd, HAILO_DISABLE_NOTIFICATION, &data);
    CHECK(0 <= res, HAILO_PCIE_DRIVER_FAIL, "HAILO_DISABLE_NOTIFICATION failed with errno: {}", errno);

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

    tCompatibleHailoIoctlData data = {};
    hailo_bar_transfer_params& transfer = data.Buffer.BarTransfer;
    transfer.transfer_direction = TRANSFER_READ;
    transfer.bar_index = static_cast<uint32_t>(bar);
    transfer.offset = offset;
    transfer.count = size;
    memset(transfer.buffer, 0, sizeof(transfer.buffer));

    CHECK(size <= sizeof(transfer.buffer), HAILO_INVALID_ARGUMENT,
        "Invalid size to read, size given {} is larger than max size {}", size, sizeof(transfer.buffer));

    if (0 > ioctl(m_fd, HAILO_BAR_TRANSFER, &data)) {
        LOGGER__ERROR("HailoRTDriver::read_bar failed with errno:{}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    memcpy(buf, transfer.buffer, transfer.count);

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::write_bar(PciBar bar, off_t offset, size_t size, const void *buf)
{
    if (size == 0) {
        LOGGER__ERROR("Invalid size to write");
        return HAILO_INVALID_ARGUMENT;
    }

    if (buf == nullptr) {
        LOGGER__ERROR("Write buffer pointer is NULL");
        return HAILO_INVALID_ARGUMENT;
    }

    tCompatibleHailoIoctlData data = {};
    hailo_bar_transfer_params& transfer = data.Buffer.BarTransfer;
    transfer.transfer_direction = TRANSFER_WRITE;
    transfer.bar_index = static_cast<uint32_t>(bar);
    transfer.offset = offset;
    transfer.count = size;
    memset(transfer.buffer, 0, sizeof(transfer.buffer));

    CHECK(size <= sizeof(transfer.buffer), HAILO_INVALID_ARGUMENT,
        "Invalid size to write, size given {} is larger than max size {}", size, sizeof(transfer.buffer));

    memcpy(transfer.buffer, buf, transfer.count);

    if (0 > ioctl(this->m_fd, HAILO_BAR_TRANSFER, &data)) {
        LOGGER__ERROR("HailoRTDriver::write_bar failed with errno: {}", errno);
        return HAILO_PCIE_DRIVER_FAIL;
    }

    return HAILO_SUCCESS;
}

Expected<uint32_t> HailoRTDriver::read_vdma_channel_registers(off_t offset, size_t size)
{
    tCompatibleHailoIoctlData data = {};
    hailo_channel_registers_params& params = data.Buffer.ChannelRegisters;
    params.transfer_direction = TRANSFER_READ;
    params.offset = offset;
    params.size = size;
    params.data = 0;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_REGISTERS, &data)) {
        LOGGER__ERROR("HailoRTDriver::read_vdma_channel_registers failed with errno: {}", errno);
        return make_unexpected(HAILO_PCIE_DRIVER_FAIL);
    }

    return std::move(params.data);
}

hailo_status HailoRTDriver::write_vdma_channel_registers(off_t offset, size_t size, uint32_t value)
{
    tCompatibleHailoIoctlData data = {};
    hailo_channel_registers_params& params = data.Buffer.ChannelRegisters;
    params.transfer_direction = TRANSFER_WRITE;
    params.offset = offset;
    params.size = size;
    params.data = value;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_REGISTERS, &data)) {
        LOGGER__ERROR("HailoRTDriver::write_vdma_channel_registers failed with errno: {}", errno);
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
    DmaDirection data_direction, uintptr_t desc_list_handle, bool enable_timestamps_measure)
{
    CHECK_AS_EXPECTED(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK_AS_EXPECTED(data_direction != DmaDirection::BOTH, HAILO_INVALID_ARGUMENT);
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_channel_enable_params& params = data.Buffer.ChannelEnable;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.direction = direction_to_dma_data_direction(data_direction);
    params.desc_list_handle = desc_list_handle,
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

    const uint32_t timestamps_count = MAX_IRQ_TIMESTAMPS_SIZE;
    struct hailo_channel_interrupt_timestamp timestamps[timestamps_count];
    tCompatibleHailoIoctlData data = {};
    hailo_vdma_channel_wait_params& params = data.Buffer.ChannelWait;
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.channel_handle = channel_handle;
    params.timeout_ms = static_cast<uint64_t>(timeout.count());
    params.timestamps = timestamps;
    params.timestamps_count = timestamps_count;

    if (0 > ioctl(this->m_fd, HAILO_VDMA_CHANNEL_WAIT_INT, &data)) {
        const auto ioctl_errno = errno;
        if (ERROR_SEM_TIMEOUT == ioctl_errno) {
            LOGGER__ERROR("Waiting for interrupt for channel {} timed-out", channel_id);
            return make_unexpected(HAILO_TIMEOUT);
        }
        if (ERROR_OPERATION_ABORTED == ioctl_errno) {
            LOGGER__INFO("Stream (index={}) was aborted!", channel_id);
            return make_unexpected(HAILO_STREAM_INTERNAL_ABORT);
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
    uint16_t desc_page_size,  uint8_t channel_index)
{
    tCompatibleHailoIoctlData data = {};
    hailo_desc_list_bind_vdma_buffer_params& config_info = data.Buffer.DescListBind;
    config_info.buffer_handle = buffer_handle;
    config_info.desc_handle = desc_handle;
    config_info.desc_page_size = desc_page_size;
    config_info.channel_index = channel_index;

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
    (void)buffer;
    (void)buffer_size;
    (void)read_bytes;
    (void)cpu_id;
    return HAILO_PCIE_NOT_SUPPORTED_ON_PLATFORM;
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
