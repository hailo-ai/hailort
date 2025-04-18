/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file user_config_common.h
 * @brief Contains information regarding the firmware user config.
**/

#ifndef __USER_CONFIG_COMMON__
#define __USER_CONFIG_COMMON__

#include <stdint.h>
#include "logger_level.h"

#define USER_CONFIG_OVERCURRENT_UNINITIALIZED_VALUE    (0)

#define USER_CONFIG_TEMPERATURE_DEFAULT_RED_ALARM_THRESHOLD (120.f)
#define USER_CONFIG_TEMPERATURE_DEFAULT_RED_HYSTERESIS_ALARM_THRESHOLD (116.f)
#define USER_CONFIG_TEMPERATURE_DEFAULT_ORANGE_ALARM_THRESHOLD (104.f)
#define USER_CONFIG_TEMPERATURE_DEFAULT_ORANGE_HYSTERESIS_ALARM_THRESHOLD (99.f)

#pragma pack(push, 1)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint8_t  entries[0];
} USER_CONFIG_header_t;

typedef struct {
    uint16_t category;
    uint16_t entry_id;
    uint32_t entry_size;
    uint8_t value[0];
} USER_CONFIG_ENTRY_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#pragma pack(pop)

// Used by user config defaults
typedef enum {
    ASPM_DISABLED = 0,
    ASPM_L1_ONLY,
    ASPM_L0S_L1
} PCIE_CONFIG_SUPPOPRTED_ASPM_STATES_t;

typedef enum {
    ASPM_L1_SUBSTATES_DISABLED = 0,
    ASPM_L1_SUBSTATES_L11_ONLY,
    ASPM_L1_SUBSTATES_L11_L12
} PCIE_CONFIG_SUPPOPRTED_L1_ASPM_SUBSTATES_t;

typedef enum {
    SOC__NN_CLOCK_400MHz = 400 * 1000 * 1000,
    SOC__NN_CLOCK_375MHz = 375 * 1000 * 1000,
    SOC__NN_CLOCK_350MHz = 350 * 1000 * 1000,
    SOC__NN_CLOCK_325MHz = 325 * 1000 * 1000,
    SOC__NN_CLOCK_300MHz = 300 * 1000 * 1000,
    SOC__NN_CLOCK_275MHz = 275 * 1000 * 1000,
    SOC__NN_CLOCK_250MHz = 250 * 1000 * 1000,
    SOC__NN_CLOCK_225MHz = 225 * 1000 * 1000,
    SOC__NN_CLOCK_200MHz = 200 * 1000 * 1000,
    SOC__NN_CLOCK_100MHz = 100 * 1000 * 1000,
    SOC__NN_CLOCK_25MHz  = 25 * 1000 * 1000
} SOC__NN_CLOCK_HZ_t;

typedef enum {
    SOC__CPU_CLOCK_200MHz = SOC__NN_CLOCK_400MHz >> 1,
    SOC__CPU_CLOCK_187MHz = SOC__NN_CLOCK_375MHz >> 1,
    SOC__CPU_CLOCK_175MHz = SOC__NN_CLOCK_350MHz >> 1,
    SOC__CPU_CLOCK_162MHz = SOC__NN_CLOCK_325MHz >> 1,
    SOC__CPU_CLOCK_150MHz = SOC__NN_CLOCK_300MHz >> 1,
    SOC__CPU_CLOCK_137MHz = SOC__NN_CLOCK_275MHz >> 1,
    SOC__CPU_CLOCK_125MHz = SOC__NN_CLOCK_250MHz >> 1,
    SOC__CPU_CLOCK_112MHz = SOC__NN_CLOCK_225MHz >> 1,
    SOC__CPU_CLOCK_100MHz = SOC__NN_CLOCK_200MHz >> 1,
    SOC__CPU_CLOCK_50MHz = SOC__NN_CLOCK_100MHz >> 1,
    SOC__CPU_CLOCK_12MHz = SOC__NN_CLOCK_25MHz >> 1
} SOC__CPU_CLOCK_HZ_t;

typedef enum {
    WD_SERVICE_MODE_HW_SW = 0,
    WD_SERVICE_MODE_HW_ONLY,
    WD_SERVICE_NUM_MODES
} WD_SERVICE_wd_mode_t;

typedef enum {
    OVERCURRENT_PARAMETERS_SOURCE_FW_VALUES = 0,
    OVERCURRENT_PARAMETERS_SOURCE_USER_CONFIG_VALUES,
    OVERCURRENT_PARAMETERS_SOURCE_BOARD_CONFIG_VALUES,
    OVERCURRENT_PARAMETERS_SOURCE_OVERCURRENT_DISABLED,
} OVERCURRENT_parameters_source_t;

typedef enum {
    OVERCURRENT_CONVERSION_PERIOD_140US = 140,
    OVERCURRENT_CONVERSION_PERIOD_204US = 204,
    OVERCURRENT_CONVERSION_PERIOD_332US = 332,
    OVERCURRENT_CONVERSION_PERIOD_588US = 588,
    OVERCURRENT_CONVERSION_PERIOD_1100US = 1100,
    OVERCURRENT_CONVERSION_PERIOD_2116US = 2116,
    OVERCURRENT_CONVERSION_PERIOD_4156US = 4156,
    OVERCURRENT_CONVERSION_PERIOD_8244US = 8244
} OVERCURRENT_conversion_time_us_t;

typedef enum {
    TEMPERATURE_PROTECTION_PARAMETERS_SOURCE_FW_VALUES = 0,
    TEMPERATURE_PROTECTION_PARAMETERS_SOURCE_USER_CONFIG_VALUES
} TEMPERATURE_PROTECTION_parameters_source_t;

#endif /* __USER_CONFIG_COMMON__ */
