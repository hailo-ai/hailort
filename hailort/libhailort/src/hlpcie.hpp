/**
 * @file pcie.h
 */

#ifndef __HLPCIE_HEADER__
#define __HLPCIE_HEADER__

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "control_protocol.h"
#include "d2h_event_queue.hpp"
#include "os/hailort_driver.hpp"

namespace hailort
{

#define FW_CODE_MAX_SIZE (0x40000)
#define FW_CODE_MIN_SIZE (20*4)
#define FW_KEY_CERTIFICATE_SIZE (0x348)
#define FW_CONTENT_CERTIFICATE_SIZE (0x5f0)


typedef uint32_t hailo_ptr_t; //Core address space is 32bit

/**
 * Writes data to device memory.
 * 
 * @param[in] dev     - A handle to the PCIe device.
 * @param[in] address - The device address to write to.
 * @param[in] buffer  - A pointer to the buffer containing the data to transfer.
 * @param[in] size    - The amount of bytes to write.
 * @return hailo_status
 */
hailo_status HAILO_PCIE__write_memory(HailoRTDriver &driver, hailo_ptr_t address, const void *buffer, uint32_t size);

/**
 * Reads data from device memory.
 * 
 * @param[in] dev      - A handle to the PCIe device.
 * @param[in] address  - The device address to read from.
 * @param[out] buffer  - A pointer to the buffer that will contain the transferred data.
 * @param[in] size     - The amount of bytes to be read.
 * @return hailo_status
 */
hailo_status HAILO_PCIE__read_memory(HailoRTDriver &driver, hailo_ptr_t address, void *buffer, uint32_t size);

hailo_status HAILO_PCIE__read_atr_to_validate_fw_is_up(HailoRTDriver &driver, bool *is_fw_up);

/**
 * Interact with the firmware.
 * 
 * @param[in]     dev               - A handle to the PCIe device to interact with.
 * @param[in]     request_buf       - A pointer to the request buffer to send to the firmware.
 * @param[in]     request_buf_size  - The size in bytes of the request buffer.
 * @param[out]    response_buf      - A pointer to the response buffer to recv from the firmware.//TODO: limitations?
 * @param[in/out] response_buf_size - A pointer to the size in bytes to recv. The number of bytes received may be less than @response_buf_size.
 *                                    Upon success, receives the number of bytes that was read; otherwise, untouched.
 * @param[in]     timeout_ms       -  Time in milliseconds to wait till response will be recived from the firmware
 * @param[in]     cpu_id           -  The CPU that will handle the control
 * @return hailo_status
 */
hailo_status HAILO_PCIE__fw_interact(HailoRTDriver &driver, const void *request, size_t request_len,  void *response_buf, 
                                     size_t *response_buf_size, uint32_t timeout_ms, hailo_cpu_id_t cpu_id);

} /* namespace hailort */

#endif /* __HLPCIE_HEADER__ */
