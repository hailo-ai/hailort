#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/logger_macros.hpp"

#include "vdma/integrated/integrated_device.hpp"
#include "vdma/vdma_config_manager.hpp"

#include "md5.h"
#include <memory>

static const std::string INTEGRATED_NNC_DRIVER_PATH = "/dev/hailo_integrated_nnc";

namespace hailort
{

bool IntegratedDevice::is_loaded()
{
#if defined(_MSC_VER)
    // windows is not supported for core driver
    return false;
#else
    return (access(INTEGRATED_NNC_DRIVER_PATH.c_str(), F_OK) == 0);
#endif // defined(_MSC_VER)
}

Expected<std::unique_ptr<IntegratedDevice>> IntegratedDevice::create()
{
    hailo_status status = HAILO_UNINITIALIZED;

    auto driver = HailoRTDriver::create(INTEGRATED_NNC_DRIVER_PATH);
    CHECK_EXPECTED(driver, "Failed to initialize HailoRTDriver");

    auto device = std::unique_ptr<IntegratedDevice>(new (std::nothrow) IntegratedDevice(driver.release(), status, DEVICE_ID));
    CHECK_AS_EXPECTED((nullptr != device), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating IntegratedDevice");

    return device;
}


IntegratedDevice::IntegratedDevice(HailoRTDriver &&driver, hailo_status &status, const std::string &device_id) : 
    VdmaDevice::VdmaDevice(std::move(driver), Device::Type::INTEGRATED, device_id)
{
    status = update_fw_state();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("update_fw_state() failed with status {}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

Expected<hailo_device_architecture_t> IntegratedDevice::get_architecture() const {
    return Expected<hailo_device_architecture_t>(m_device_architecture);
}

hailo_status IntegratedDevice::reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type)
{
    if (CONTROL_PROTOCOL__RESET_TYPE__NN_CORE == reset_type) {
        return m_driver.reset_nn_core();
    }

    LOGGER__ERROR("Can't reset IntegratedDevice, please use linux reboot");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<size_t> IntegratedDevice::read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id)
{
    if (hailo_cpu_id_t::HAILO_CPU_ID_0 == cpu_id) {
        LOGGER__ERROR("Read FW log is supported only on core CPU");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    return VdmaDevice::read_log(buffer, cpu_id);
}

} /* namespace hailort */