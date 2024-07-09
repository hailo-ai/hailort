#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/logger_macros.hpp"

#include "vdma/integrated/integrated_device.hpp"
#include "vdma/vdma_config_manager.hpp"

#include "md5.h"
#include <memory>

namespace hailort
{

// 16 MB 
#define INTEGRATED_DEVICE_INFINITE_ACTION_LIST_POOL_SIZE (16777216)

bool IntegratedDevice::is_loaded()
{
    return HailoRTDriver::is_integrated_nnc_loaded();
}

Expected<std::pair<void*, uint64_t>> IntegratedDevice::allocate_infinite_action_list_buffer(size_t size)
{
    CHECK_AS_EXPECTED(0 == (size % OsUtils::get_page_size()), HAILO_INVALID_ARGUMENT,
        "Infinte action list buffer size must be a multiple of page size");
    CHECK_AS_EXPECTED(m_device_infinite_action_list_pool_allocation_offset + size <= m_device_infinite_action_list_pool.size(),
        HAILO_INVALID_ARGUMENT, "Buffer pool size is too small for requested infinte action list buffer");

    auto user_addres = static_cast<void*>(reinterpret_cast<uint8_t*>(m_device_infinite_action_list_pool.user_address()) +
        m_device_infinite_action_list_pool_allocation_offset);
    auto dma_address = m_device_infinite_action_list_pool.dma_address() + m_device_infinite_action_list_pool_allocation_offset;

    m_device_infinite_action_list_pool_allocation_offset += size;

    return std::make_pair(user_addres, dma_address);
}

Expected<std::unique_ptr<IntegratedDevice>> IntegratedDevice::create()
{
    hailo_status status = HAILO_UNINITIALIZED;

    TRY(auto driver, HailoRTDriver::create_integrated_nnc());

    // Create pool of memory for infinite action list so can all be in the LUT memory area
    // TODO: remove this when infinite action list allocates from its own pool of CMA memory
    TRY(auto infinite_action_list_pool, vdma::ContinuousBuffer::create(INTEGRATED_DEVICE_INFINITE_ACTION_LIST_POOL_SIZE,
        *driver));
    
    // Verify pool is in mapped range
    CHECK_AS_EXPECTED(DDRActionListBufferBuilder::verify_dma_addr(infinite_action_list_pool), HAILO_INTERNAL_FAILURE,
        "Failed to allocate continous buffer pool M4 mapped memory region");


    auto device = std::unique_ptr<IntegratedDevice>(new (std::nothrow) IntegratedDevice(std::move(driver),
        std::move(infinite_action_list_pool), status));
    CHECK_AS_EXPECTED((nullptr != device), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating IntegratedDevice");

    return device;
}

IntegratedDevice::IntegratedDevice(std::unique_ptr<HailoRTDriver> &&driver, vdma::ContinuousBuffer &&pool,
                                   hailo_status &status) :
    VdmaDevice::VdmaDevice(std::move(driver), Device::Type::INTEGRATED, status),
    m_device_infinite_action_list_pool(std::move(pool)),
    m_device_infinite_action_list_pool_allocation_offset(0)
{
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to create VdmaDevice");
        return;
    }

    status = update_fw_state();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("update_fw_state() failed with status {}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status IntegratedDevice::reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type)
{
    if (CONTROL_PROTOCOL__RESET_TYPE__NN_CORE == reset_type) {
        return m_driver->reset_nn_core();
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