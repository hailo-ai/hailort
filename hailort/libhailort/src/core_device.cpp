#include "core_device.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/logger_macros.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "md5.h"

#include <memory>

static const std::chrono::milliseconds CORE_DEFAULT_TIMEOUT = std::chrono::milliseconds(1000);
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

    auto device = std::unique_ptr<CoreDevice>(new (std::nothrow) CoreDevice(driver.release(), status));
    CHECK_AS_EXPECTED((nullptr != device), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating CoreDevice");

    return device;
}


CoreDevice::CoreDevice(HailoRTDriver &&driver, hailo_status &status) : 
    VdmaDevice::VdmaDevice(std::move(driver), Device::Type::CORE)
{
    status = update_fw_state();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("update_fw_state() failed with status {}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

CoreDevice::~CoreDevice() {}

Expected<hailo_device_architecture_t> CoreDevice::get_architecture() const {
    return Expected<hailo_device_architecture_t>(m_device_architecture);
}

hailo_status CoreDevice::fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id)
{
    uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH];
    MD5_CTX ctx;

    // TODO HRT-5358 - Unify MD5 functions. Use by pcie and core driver (and FW)
    MD5_Init(&ctx);
    MD5_Update(&ctx, request_buffer, request_size);
    MD5_Final(request_md5, &ctx);

    uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH];
    uint8_t expected_response_md5[PCIE_EXPECTED_MD5_LENGTH];

    auto status = m_driver.fw_control(request_buffer, request_size, request_md5,
        response_buffer, response_size, response_md5,
        CORE_DEFAULT_TIMEOUT, cpu_id);
    CHECK_SUCCESS(status, "Failed to send fw control");

    MD5_Init(&ctx);
    MD5_Update(&ctx, response_buffer, (*response_size));
    MD5_Final(expected_response_md5, &ctx);

    auto memcmp_result = memcmp(expected_response_md5, response_md5, sizeof(response_md5));
    if (0 != memcmp_result) {
        LOGGER__ERROR("MD5 validation of control reesponse failed.");
        return HAILO_INTERNAL_FAILURE;
    }

    return HAILO_SUCCESS;
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