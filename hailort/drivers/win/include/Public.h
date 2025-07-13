/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

#ifndef _HAILO_PUBLIC_H_
#define _HAILO_PUBLIC_H_


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

#include "..\..\common\hailo_ioctl_common.h"

#endif /* _HAILO_PUBLIC_H_ */