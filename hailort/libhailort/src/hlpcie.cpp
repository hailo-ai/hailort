#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include "hw_consts.hpp"
#include "hlpcie.hpp"
#include "common/circular_buffer.hpp"
#include "common/logger_macros.hpp"
#include "d2h_events.h"
#include "firmware_header.h"
#include "os/hailort_driver.hpp"
#include "md5.h"

namespace hailort
{

/*******************************************************************************
 * pcie control configurations
********************************************************************************/
typedef enum {
    ATR_PARAM_SIZE_4KB = 11,
    ATR_PARAM_SIZE_8KB
} ATR_PARAM_SIZE_t;

typedef enum {
    ATR0 = 0,
    ATR1
} ATR_TABLES_t;

typedef enum {
    TRSL_ID_AXI4_MASTER_0 = 4,
    TRSL_ID_AXI4_MASTER_1,
    TRSL_ID_AXI4_MASTER_2,
    TRSL_ID_AXI4_MASTER_3,
} TRSL_ID_t;

#define PCIE_SRAM_TABLE_SIZE (4*1024)
#define PCIE_SRAM_BAR_4_OFFSET (0)
#define PCIE_BLOCK_BAR_4_OFFSET (PCIE_SRAM_BAR_4_OFFSET + PCIE_SRAM_TABLE_SIZE)

#define PCIE_REQUEST_SIZE  (0x640)
#define PCIE_RESPONSE_SIZE (0x640)
#define PCIE_D2H_EVENT_MESSAGE_SRAM_OFFSET (PCIE_REQUEST_SIZE + PCIE_RESPONSE_SIZE) 

/* SRC_ADDR needs to be shifted by 12 since it takes only bits [31:12] from it. */
#define PCI_BIT_SHIFT_FOR_4KB_GRANULARITY (12)
#define PCIE_SRAM_BAR_4_SRC_ADDR (PCIE_SRAM_BAR_4_OFFSET >> PCI_BIT_SHIFT_FOR_4KB_GRANULARITY)
#define PCIE_BLOCK_BAR_4_SRC_ADDR (PCIE_BLOCK_BAR_4_OFFSET >> PCI_BIT_SHIFT_FOR_4KB_GRANULARITY)

#define ATR0_TABLE_SIZE (PCIE_SRAM_TABLE_SIZE)
#define ATR1_OFFSET     (ATR0_TABLE_SIZE)
#define ATR0_PCIE_BRIDGE_OFFSET (0x700)
#define ATR1_PCIE_BRIDGE_OFFSET (ATR0_PCIE_BRIDGE_OFFSET + 0x20)

/* atr0 table is configured to the SRAM */
#define ATR0_SRC_ADDR       (0x0)
#define HAILO_MERCURY_FW_CONTROL_ADDRESS (0x000BE000)
#define HAILO8_FW_CONTROL_ADDRESS        (0x60000000)
#define ATR0_TRSL_ADDR2     (0x0)

/* atr1 table is configured to the PCIe block */
#define ATR1_SRC_ADDR       (0x0)
// The address macro uses __VA_ARGS__ that doesn't expand correctly with C++ if no args were given, so we must pass the default values explicitly.
#define ATR1_TRSL_ADDR1     (PCIE_CONFIG_BASE_ADDRESS)
#define ATR1_TRSL_ADDR2     (0x0)

typedef struct {
    uint32_t atr_param;
    uint32_t atr_src;
    uint32_t atr_trsl_addr_1;
    uint32_t atr_trsl_addr_2;
    uint32_t atr_trsl_param;
} hailo_pcie_atr_config_t;

static uint32_t fw_control_ram_address_by_board_type(HailoRTDriver::BoardType board_type) {
    switch (board_type) {
    case HailoRTDriver::BoardType::HAILO8:
        return HAILO8_FW_CONTROL_ADDRESS;
    case HailoRTDriver::BoardType::MERCURY:
        return HAILO_MERCURY_FW_CONTROL_ADDRESS;
    default:
        assert(true);
        return 0;
    }
}

// TODO HRT-6392: validate atr in driver, remove hw-consts
inline void get_atr_param_config(HailoRTDriver &driver, ATR_TABLES_t atr_table, hailo_pcie_atr_config_t *atr_config)
 {
    uint64_t param = 0;
    assert(atr_config != NULL);
    memset(atr_config, 0, sizeof(hailo_pcie_atr_config_t));

    switch(atr_table) {
        case ATR0:
            PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__ATR_IMPL__SET(param);
            PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__ATR_SIZE__MODIFY(param, ATR_PARAM_SIZE_4KB);
            PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR0_PCIE_WIN1__SOURCE_ADDR__MODIFY(param, PCIE_SRAM_BAR_4_SRC_ADDR);
            atr_config->atr_param = (uint32_t)param;
            atr_config->atr_src = ATR0_SRC_ADDR;
            atr_config->atr_trsl_addr_1 = fw_control_ram_address_by_board_type(driver.board_type());
            atr_config->atr_trsl_addr_2 = ATR0_TRSL_ADDR2;
            atr_config->atr_trsl_param = TRSL_ID_AXI4_MASTER_2;
            break;

        case ATR1:
            PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__ATR_IMPL__SET(param);
            PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__ATR_SIZE__MODIFY(param, ATR_PARAM_SIZE_4KB);
            PCIE_BRIDGE_CONFIG__ATR_PARAM_ATR1_PCIE_WIN1__SOURCE_ADDR__MODIFY(param, PCIE_BLOCK_BAR_4_SRC_ADDR);
            atr_config->atr_param = (uint32_t)param;
            atr_config->atr_src = ATR1_SRC_ADDR;
            atr_config->atr_trsl_addr_1 = ATR1_TRSL_ADDR1;
            atr_config->atr_trsl_addr_2 = ATR1_TRSL_ADDR2;
            atr_config->atr_trsl_param = TRSL_ID_AXI4_MASTER_2;
            break;

        default:
            LOGGER__ERROR("table param configuration not supoorted");
    }
   
}
/*******************************************************************************
 * Private Functions
*******************************************************************************/
// TODO HRT-5358 - Unify MD5 functions. Use by pcie and core driver (and FW)
void hailo_pcie__set_MD5( const uint8_t  *buffer, uint32_t buffer_length, unsigned char expected_md5[16])
{
    MD5_CTX ctx;

    MD5_Init(&ctx);
    MD5_Update(&ctx, buffer, buffer_length);
    MD5_Final(expected_md5, &ctx);
}

hailo_status hailo_pcie__check_atr_configuration(HailoRTDriver &driver,  hailo_pcie_atr_config_t *atr_config_to_validate, ATR_TABLES_t atr_table)
{
    uint32_t offset = 0;
    hailo_pcie_atr_config_t pcie_atr_table;

    if (NULL == atr_config_to_validate) {
        return HAILO_INVALID_ARGUMENT;
    }

    switch(atr_table) {
        case ATR0:
            offset = ATR0_PCIE_BRIDGE_OFFSET;
            break;

        case ATR1:
            offset = ATR1_PCIE_BRIDGE_OFFSET;
            break;

        default:
            LOGGER__ERROR("table param configuration not supported");
    }

    hailo_status status = driver.read_bar(PciBar::bar0, offset , sizeof(pcie_atr_table), &pcie_atr_table);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Reading SRAM table Failed");
        return status;
    }
    /* Check that ATR was configured to as the wanted configuration */
    if (0 != memcmp(atr_config_to_validate, &pcie_atr_table, sizeof(pcie_atr_table))){
        LOGGER__ERROR("|==================+================+===============|");
        LOGGER__ERROR("|              ATR{} is misconfigured                |", atr_table);
        LOGGER__ERROR("|------------------+----------------+---------------|");
        LOGGER__ERROR("|  Field           +  Expected      +  Read         |");
        LOGGER__ERROR("|------------------+----------------+---------------|");
        LOGGER__ERROR("|  ATR_PARM        |  0x{:08X}    |  0x{:08X}   |", atr_config_to_validate->atr_param, pcie_atr_table.atr_param);
        LOGGER__ERROR("|  ATR_SRC_ADDR    |  0x{:08X}    |  0x{:08X}   |", atr_config_to_validate->atr_src, pcie_atr_table.atr_src);
        LOGGER__ERROR("|  ATR_TRSL_ADDR1  |  0x{:08X}    |  0x{:08X}   |", atr_config_to_validate->atr_trsl_addr_1, pcie_atr_table.atr_trsl_addr_1);
        LOGGER__ERROR("|  ATR_TRSL_ADDR2  |  0x{:08X}    |  0x{:08X}   |", atr_config_to_validate->atr_trsl_addr_2, pcie_atr_table.atr_trsl_addr_2);
        LOGGER__ERROR("|  ATR_TRSL_PARAM  |  0x{:08X}    |  0x{:08X}   |", atr_config_to_validate->atr_trsl_param, pcie_atr_table.atr_trsl_param);
        LOGGER__ERROR("|==================+================+===============|");
        return HAILO_ATR_TABLES_CONF_VALIDATION_FAIL;
    }
    return HAILO_SUCCESS;
}

hailo_status restore_atr_config(HailoRTDriver &driver, hailo_pcie_atr_config_t *old_atr_config)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = driver.write_bar(PciBar::bar0, ATR0_PCIE_BRIDGE_OFFSET, sizeof(*old_atr_config), old_atr_config);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }
    status = hailo_pcie__check_atr_configuration(driver, old_atr_config, ATR0);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("BAR_4 wasn't configured correctly");
        goto l_exit;
    }
l_exit:
    return status;
}

hailo_status config_atr_for_direct_memory_access(HailoRTDriver &driver, uint32_t base_address)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_pcie_atr_config_t new_atr_config = {};

    get_atr_param_config(driver, ATR0, &new_atr_config);
    new_atr_config.atr_trsl_addr_1 = base_address;

    // Config BAR0 to the new ATR-configuration, and validate configuration
    status = driver.write_bar(PciBar::bar0, ATR0_PCIE_BRIDGE_OFFSET, sizeof(new_atr_config), &new_atr_config);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }
    
    status = hailo_pcie__check_atr_configuration(driver, &new_atr_config, ATR0);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("BAR_4 wasn't configured correctly");
        goto l_exit;
    }

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

// HRT-6393 move read/write memory to driver
hailo_status hailo_pcie__write_memory(HailoRTDriver &driver, uint32_t base_address, uint32_t offset,
    const void *buffer, uint32_t size)
{
    hailo_status status = HAILO_UNINITIALIZED;

    assert(0 == (base_address & (ATR0_TABLE_SIZE - 1)));
    assert((offset + size) <= ATR0_TABLE_SIZE);

    status = config_atr_for_direct_memory_access(driver, base_address);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }

    status = driver.write_bar(PciBar::bar4, offset, size, buffer);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

hailo_status HAILO_PCIE__write_memory(HailoRTDriver &driver, hailo_ptr_t address, const void *buffer, uint32_t size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint32_t offset = 0;
    uint32_t chunk_size = 0;
    hailo_pcie_atr_config_t old_atr_config = {};
    uint32_t base_address = address & ~(ATR0_TABLE_SIZE - 1);


    // Save the old ATR-configuration to old_atr_config
    status = driver.read_bar(PciBar::bar0, ATR0_PCIE_BRIDGE_OFFSET , sizeof(old_atr_config), &old_atr_config);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }

    // Write memory
    offset = 0;
    if (base_address != address) {
        chunk_size = MIN(base_address + ATR0_TABLE_SIZE - address, size);
        status = hailo_pcie__write_memory(driver, base_address, address - base_address, buffer, chunk_size);
        if (HAILO_SUCCESS != status) {
            goto l_cleanup;
        }
        offset += chunk_size;
    }
    while (offset < size) {
        chunk_size = MIN((size - offset), ATR0_TABLE_SIZE);
        status = hailo_pcie__write_memory(driver, address + offset, 0, (void*)((uintptr_t)buffer + offset), chunk_size);
        if (HAILO_SUCCESS != status) {
            goto l_cleanup;
        }
        offset += chunk_size;
    }

    status = HAILO_SUCCESS;
l_cleanup:
    status = restore_atr_config(driver, &old_atr_config);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to reconfigure BAR_4 after direct memory access");
    }
l_exit:
    return status;
}

hailo_status HAILO_PCIE__read_atr_to_validate_fw_is_up(HailoRTDriver &driver, bool *is_fw_up)
{
    hailo_pcie_atr_config_t atr_config = {};

    hailo_status status = driver.read_bar(PciBar::bar0, ATR0_PCIE_BRIDGE_OFFSET , sizeof(atr_config), &atr_config);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Reading SRAM table Failed\n");
        *is_fw_up = false;
        return status;
    }

    *is_fw_up = (fw_control_ram_address_by_board_type(driver.board_type()) == atr_config.atr_trsl_addr_1);
    return HAILO_SUCCESS;
}

hailo_status hailo_pcie__read_memory(HailoRTDriver &driver, uint32_t base_address, uint32_t offset, void *buffer,
    uint32_t size)
{
    hailo_status status = HAILO_UNINITIALIZED;

    assert(0 == (base_address & (ATR0_TABLE_SIZE - 1)));
    assert((offset + size) <= ATR0_TABLE_SIZE);

    status = config_atr_for_direct_memory_access(driver, base_address);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }

    status = driver.read_bar(PciBar::bar4, offset, size, buffer);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

hailo_status HAILO_PCIE__read_memory(HailoRTDriver &driver, hailo_ptr_t address, void *buffer, uint32_t size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint32_t offset = 0;
    uint32_t chunk_size = 0;
    uint32_t base_address = address & ~(ATR0_TABLE_SIZE - 1);
    hailo_pcie_atr_config_t old_atr_config = {};

    // Save the old ATR-configuration to old_atr_config
    status = driver.read_bar(PciBar::bar0, ATR0_PCIE_BRIDGE_OFFSET , sizeof(old_atr_config),
        &old_atr_config);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }

    // Read memory
    offset = 0;
    if (base_address != address) {
        chunk_size = MIN(base_address + ATR0_TABLE_SIZE - address, size);
        status = hailo_pcie__read_memory(driver, base_address, address - base_address, buffer, chunk_size);
        if (HAILO_SUCCESS != status) {
            goto l_cleanup;
        }
        offset += chunk_size;
    }
    while (offset < size) {
        chunk_size = MIN((size - offset), ATR0_TABLE_SIZE);
        status = hailo_pcie__read_memory(driver, address + offset, 0, (void*)((uintptr_t)buffer + offset), chunk_size);
        if (HAILO_SUCCESS != status) {
            goto l_cleanup;
        }
        offset += chunk_size;
    }

    status = HAILO_SUCCESS;
l_cleanup:
    status = restore_atr_config(driver, &old_atr_config);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to reconfigure BAR_4 after direct memory access");
    }
l_exit:
    return status;
}

hailo_status HAILO_PCIE__fw_interact(HailoRTDriver &driver, const void *request, size_t request_len,
    void *response_buf, size_t *response_buf_size, uint32_t timeout_ms, hailo_cpu_id_t cpu_id)
{
    /* Validate ATR0 configuration before writing to BAR4 SRAM*/
    hailo_pcie_atr_config_t atr_config;
    get_atr_param_config(driver, ATR0, &atr_config);
    auto status = hailo_pcie__check_atr_configuration(driver, &atr_config, ATR0);
    CHECK_SUCCESS(status, "Validate address translation tables Failed, For FW control use.");

    /* Validate ATR1 configuration before writing to BAR4 PCIe bridge block */
    get_atr_param_config(driver, ATR1, &atr_config);
    status = hailo_pcie__check_atr_configuration(driver, &atr_config, ATR1);
    CHECK_SUCCESS(status, "Validate address translation tables Failed, For FW control use.");

    /* Send control */
    uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH];
    hailo_pcie__set_MD5((uint8_t *)request, static_cast<uint32_t>(request_len), request_md5);
    uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH];
    status = driver.fw_control(request, request_len, request_md5,
        response_buf, response_buf_size, response_md5,
        std::chrono::milliseconds(timeout_ms), cpu_id);
    CHECK_SUCCESS(status, "Failed to send fw control");

    /* Validate response MD5 */
    uint8_t calculated_md5_sum[PCIE_EXPECTED_MD5_LENGTH];
    hailo_pcie__set_MD5((uint8_t *)response_buf, static_cast<uint32_t>(*response_buf_size), calculated_md5_sum);
    auto memcmp_result = memcmp(calculated_md5_sum, response_md5, sizeof(calculated_md5_sum));
    CHECK(0 == memcmp_result, HAILO_CONTROL_RESPONSE_MD5_MISMATCH, "Control response md5 not valid");

    return HAILO_SUCCESS;
}

} /* namespace hailort */
