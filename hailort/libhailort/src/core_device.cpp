#include "core_device.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/logger_macros.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "md5.h"

#include <memory>

static const std::string CORE_DRIVER_PATH = "/dev/hailo_core";

namespace hailort
{

bool CoreDevice::is_loaded()
{
#if defined(_MSC_VER)
    // windows is not supported for core driver
    return false;
#else
    return (access(CORE_DRIVER_PATH.c_str(), F_OK) == 0);
#endif // defined(_MSC_VER)
}

Expected<std::unique_ptr<CoreDevice>> CoreDevice::create()
{
    hailo_status status = HAILO_UNINITIALIZED;

    auto driver = HailoRTDriver::create(CORE_DRIVER_PATH);
    CHECK_EXPECTED(driver, "Failed to initialize HailoRTDriver");

    auto device = std::unique_ptr<CoreDevice>(new (std::nothrow) CoreDevice(driver.release(), status, DEVICE_ID));
    CHECK_AS_EXPECTED((nullptr != device), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating CoreDevice");

    return device;
}


CoreDevice::CoreDevice(HailoRTDriver &&driver, hailo_status &status, const std::string &device_id) : 
    VdmaDevice::VdmaDevice(std::move(driver), Device::Type::CORE, device_id)
{
    status = update_fw_state();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("update_fw_state() failed with status {}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

Expected<hailo_device_architecture_t> CoreDevice::get_architecture() const {
    return Expected<hailo_device_architecture_t>(m_device_architecture);
}

hailo_status CoreDevice::reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type)
{
    if (CONTROL_PROTOCOL__RESET_TYPE__NN_CORE == reset_type) {
        return m_driver.reset_nn_core();
    }

    LOGGER__ERROR("Can't reset CoreDevice, please use linux reboot");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<size_t> CoreDevice::read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id)
{
    if (hailo_cpu_id_t::HAILO_CPU_ID_0 == cpu_id) {
        LOGGER__ERROR("Read FW log is supported only on core CPU");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    return VdmaDevice::read_log(buffer, cpu_id);
}

} /* namespace hailort */