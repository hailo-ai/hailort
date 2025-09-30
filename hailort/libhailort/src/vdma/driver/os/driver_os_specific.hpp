/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file driver_os_specific.hpp
 * @brief Contains some functions for hailort driver which have OS specific implementation.
 **/

#ifndef _HAILO_DRIVER_OS_SPECIFIC_HPP_
#define _HAILO_DRIVER_OS_SPECIFIC_HPP_

#include "hailo/expected.hpp"
#include "common/file_descriptor.hpp"
#include "vdma/driver/hailort_driver.hpp"

#ifdef _WIN32
#include "hailo_ioctl_common.h" //  for tCompatibleHailoIoctlData
#endif

namespace hailort
{

Expected<FileDescriptor> open_device_file(const std::string &path);
Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name);
Expected<std::vector<HailoRTDriver::DeviceInfo>> scan_nnc_devices();
Expected<std::vector<HailoRTDriver::DeviceInfo>> scan_soc_devices();

hailo_status convert_errno_to_hailo_status(int err, const char* ioctl_name);

#ifndef _WIN32

// Runs the ioctl, returns errno value (or 0 on success)
int run_hailo_ioctl(underlying_handle_t file, uint32_t ioctl_code, void *param);

#else /* _WIN32 */

/**
 * On windows, all IOCTLs shares the same structure for input and output (tCompatibleHailoIoctlData).
 * To make windows and posix code the same, we need to convert the actual structure type 
 * This template static class is used to covert to compatible (for input parameters) and from compatible (for output
 * parameters).
 */
template<typename PointerType>
class WindowsIoctlParamCast final {
public:
    static tCompatibleHailoIoctlData to_compatible(PointerType param_ptr);
    static void from_compatible(const tCompatibleHailoIoctlData& data, PointerType param_ptr);
};


int run_ioctl_compatible_data(underlying_handle_t file, uint32_t ioctl_code, tCompatibleHailoIoctlData& data);

// Runs the ioctl, returns GetLastError() value (or 0 on success)
template<typename PointerType>
int run_hailo_ioctl(underlying_handle_t file, uint32_t ioctl_code, PointerType param)
{
    static_assert(
        (std::is_pointer<PointerType>::value) || (std::is_same<PointerType, nullptr_t>::value),
        "run_ioctl is accepting only pointer or nullptr_t as param");

    tCompatibleHailoIoctlData data = WindowsIoctlParamCast<PointerType>::to_compatible(param);
    int result = run_ioctl_compatible_data(file, ioctl_code, data);
    WindowsIoctlParamCast<PointerType>::from_compatible(data, param);
    return result;
}

#endif

} /* namespace hailort */

#endif /* _HAILO_DRIVER_OS_SPECIFIC_HPP_ */
