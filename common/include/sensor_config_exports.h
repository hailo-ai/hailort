/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sensor_config_exports.h
 * @brief Sensor config exported defines.
**/

#ifndef __SENSOR_CONFIG_EXPORT__
#define __SENSOR_CONFIG_EXPORT__


#define MAX_CONFIG_NAME_LEN 100 

#pragma pack(push, 1)
typedef struct {
    uint8_t operation;
    uint8_t length; //how many bits of the register
    uint8_t page;  //If SOC is limited to 8 bit addressing, or some array imager.
    uint32_t address;
    uint32_t bitmask;
    uint32_t value; //8/16/32-bit register value
} SENSOR_CONFIG__operation_cfg_t;
#pragma pack(pop)

typedef struct {
    uint32_t sensor_type;
    uint32_t config_size; 
    uint16_t reset_config_size;
    uint8_t is_free;
    uint8_t no_reset_offset;
    uint16_t config_height;
    uint16_t config_width;
    uint16_t config_fps;
    uint16_t section_version;
    uint8_t config_name[MAX_CONFIG_NAME_LEN];
} SENSOR_CONFIG__section_info_t; 

typedef enum {
    SENSOR_CONFIG_OPCODES_WR = 0,
    SENSOR_CONFIG_OPCODES_RD,
    SENSOR_CONFIG_OPCODES_RMW,
    SENSOR_CONFIG_OPCODES_DELAY,
} SENSOR_CONFIG_OPCODES_t;

typedef struct {
    uint8_t bus_index;
    uint16_t slave_address;
    uint8_t register_address_size;
    bool should_hold_bus;
    uint8_t endianness;
} SENSOR_I2C_SLAVE_INFO_t;

typedef enum {
    I2C_SLAVE_ENDIANNESS_BIG_ENDIAN = 0,
    I2C_SLAVE_ENDIANNESS_LITTLE_ENDIAN = 1,

    I2C_SLAVE_ENDIANNESS_COUNT
} i2c_slave_endianness_t;

#define SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT   (8)
#define SENSOR_CONFIG__SENSOR_SECTION_BLOCK_COUNT   (7)
#define SENSOR_CONFIG__ISP_SECTIONS_BLOCK_COUNT     (SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT - SENSOR_CONFIG__SENSOR_SECTION_BLOCK_COUNT)
#define SENSOR_CONFIG__ISP_SECTION_INDEX            (SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT - SENSOR_CONFIG__ISP_SECTIONS_BLOCK_COUNT)
#define SENSOR_SECTIONS_INFO_SIZE                   (SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT * sizeof(SENSOR_CONFIG__section_info_t))

#endif /* __SENSOR_CONFIG_EXPORT__ */