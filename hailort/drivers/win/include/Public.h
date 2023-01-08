/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_HailoKM,
    0xd88d31f1,0xfede,0x4e71,0xac,0x2a,0x6c,0xe0,0x01,0x8c,0x15,0x01);
// {d88d31f1-fede-4e71-ac2a-6ce0018c1501}

#define HAILO_IOCTL_COMMON                      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_FUNC(x)                           (((x) >> 2) & 0xfff)
#define NUMBER_OF_PARAMETERS(code)              ((code) & 0xf)
#define NUMBER_OF_PARAMETERS_FLEXIBLE           0xf

struct tCommonHailoIoctlParam
{
    ULONG ulCode     : 24;
    ULONG fResponse  : 1;
    ULONG fUseLL     : 1;
    ULONG ulParamNum : 4;
    union {
        ULONG ulInputs[4];
        ULONGLONG llInputs[2];
    };
    union {
        ULONG ulOutputs[4];
        ULONGLONG llOutputs[2];
    };
};

#define HAILO_CMD_FW_LOAD               0x0010
#define HAILO_CMD_READ_CFG              0x0011
#define HAILO_CMD_SW_RESET              0x0020
#define HAILO_CMD_READ_INTERRUPT_BAR    0x0021
#define HAILO_CMD_READ_FW_STATUS        0x0030
#define HAILO_CMD_READ_FIRMWARE_BAR     0x0031
#define HAILO_CMD_CANCEL_READ           0x0040
#define HAILO_CMD_READ_RP_CFG           0x0041
#define HAILO_CMD_UNMAP_BUFFER          0x0050
#define HAILO_CMD_MAP_BUFFER            0x0051
#define HAILO_CMD_FREE_MEMORY           0x0060
#define HAILO_CMD_ALLOC_MEMORY          0x0061
#define HAILO_CMD_ABORT_ALL             0x0070

#define HAILO_IOCTL_COMPATIBLE                  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
struct tCompatibleHailoIoctlParam
{
    union {
        struct {
            ULONG Size : 16;
            ULONG Code : 8;
            ULONG Type : 6;
            ULONG Read : 1;
            ULONG Write : 1;
        } bits;
        ULONG value;
    } u;
};

#define HAILO_GENERAL_IOCTL_MAGIC 0
#define HAILO_VDMA_IOCTL_MAGIC    1
#define HAILO_NON_LINUX_IOCTL_MAGIC 2



static ULONG FORCEINLINE _IOC_(ULONG nr, ULONG type, ULONG size, bool read, bool write)
{
    tCompatibleHailoIoctlParam param;
    param.u.bits.Code = nr;
    param.u.bits.Size = size;
    param.u.bits.Type = type;
    param.u.bits.Read = read ? 1 : 0;
    param.u.bits.Write = write ? 1 : 0;
    return param.u.value;
}

#define _IOW_(type,nr,size) _IOC_(nr, type, sizeof(size), true, false)
#define _IOR_(type,nr,size) _IOC_(nr, type, sizeof(size), false, true)
#define _IOWR_(type,nr,size) _IOC_(nr, type, sizeof(size), true, true)
#define _IO_(type,nr) _IOC_(nr, type, 0, false, false)

#include "..\..\common\hailo_ioctl_common.h"

struct tCompatibleHailoIoctlData
{
    tCompatibleHailoIoctlParam Parameters;
    ULONG_PTR Value;
    union {
        hailo_memory_transfer_params MemoryTransfer;
        hailo_vdma_channel_enable_params ChannelEnable;
        hailo_vdma_channel_disable_params ChannelDisable;
        hailo_vdma_channel_wait_params ChannelWait;
        hailo_vdma_channel_abort_params ChannelAbort;
        hailo_vdma_channel_clear_abort_params ChannelClearAbort;
        hailo_vdma_buffer_sync_params VdmaBufferSync;
        hailo_fw_control FirmwareControl;
        hailo_vdma_buffer_map_params VdmaBufferMap;
        hailo_vdma_buffer_unmap_params VdmaBufferUnmap;
        hailo_desc_list_create_params DescListCreate;
        uintptr_t DescListReleaseParam;
        hailo_desc_list_bind_vdma_buffer_params DescListBind;
        hailo_d2h_notification D2HNotification;
        hailo_device_properties DeviceProperties;
        hailo_driver_info DriverInfo;
        hailo_vdma_channel_read_register_params ChannelRegisterRead;
        hailo_vdma_channel_write_register_params ChannelRegisterWrite;
        hailo_non_linux_desc_list_mmap_params DescListMmap;
        hailo_read_log_params ReadLog;
        hailo_mark_as_in_use_params MarkAsInUse;
    } Buffer;
};
