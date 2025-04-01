/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file power_measurement_example.c
 * This example demonstrates power and current measurements.
 **/

#include "common.h"
#include "hailo/hailort.h"

#include <string.h>

#define SAMPLING_PERIOD (HAILO_SAMPLING_PERIOD_1100US)
#define AVERAGE_FACTOR (HAILO_AVERAGE_FACTOR_256)
#define DVM_OPTION (HAILO_DVM_OPTIONS_AUTO) // For current measurement over EVB - pass DVM explicitly (see hailo_dvm_options_t)
#define MEASUREMENT_BUFFER_INDEX (HAILO_MEASUREMENT_BUFFER_INDEX_0)

#define MEASUREMENTS_DURATION_SECS (5)
#define MAX_DEVICES (16)

#define MEASUREMENT_UNITS(__type) \
    ((HAILO_POWER_MEASUREMENT_TYPES__POWER == __type) ? ("W") : ("mA"))

#define USAGE_ERROR_MSG ("Args parsing error.\nUsage: power_measurement_example [power / current]\n" \
    "* power   - measure power consumption in W\n" \
    "* current - measure current in mA\n")

#define POWER_ARG "power"
#define CURRENT_ARG "current"


void sleep_seconds(uint32_t duration_seconds)
{
#if defined(__unix__) || defined(__QNX__)
    sleep(duration_seconds);
#else
    Sleep(duration_seconds);
#endif
}

void parse_arguments(int argc, char **argv, hailo_power_measurement_types_t *measurement_type)
{
    if (2 != argc) {
        printf(USAGE_ERROR_MSG);
        exit(1);
    }

    if (0 == strncmp(POWER_ARG, argv[1], ARRAY_LENGTH(POWER_ARG))) {
        *measurement_type = HAILO_POWER_MEASUREMENT_TYPES__POWER;
    } else if (0 == strncmp(CURRENT_ARG, argv[1], ARRAY_LENGTH(CURRENT_ARG))) {
        *measurement_type = HAILO_POWER_MEASUREMENT_TYPES__CURRENT;
    } else {
        printf(USAGE_ERROR_MSG);
        exit(1);
    }
}

hailo_status print_measurements_results(hailo_device device, hailo_power_measurement_data_t *result, hailo_power_measurement_types_t type)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_device_id_t id = {0};
    status = hailo_get_device_id(device, &id);
    REQUIRE_SUCCESS(status, l_exit, "Failed to get device id");
    const char* type_str = (type == HAILO_POWER_MEASUREMENT_TYPES__POWER) ? "Power measurement" :
        "Current measurement";

    printf("Device %s:\n", id.id);
    printf("  %s\n", type_str);
    printf("    Minimum value: %f %s\n", result->min_value, MEASUREMENT_UNITS(type));
    printf("    Average value: %f %s\n", result->average_value, MEASUREMENT_UNITS(type));
    printf("    Maximum value: %f %s\n", result->max_value, MEASUREMENT_UNITS(type));

l_exit:
    return status;
}

int main(int argc, char **argv)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_vdevice vdevice = NULL;
    hailo_device_id_t device_ids[MAX_DEVICES];
    size_t actual_device_count = MAX_DEVICES;
    hailo_vdevice_params_t params = {0};
    hailo_device physical_devices[MAX_DEVICES];
    hailo_power_measurement_data_t measurement_result[MAX_DEVICES] = {0};
    hailo_power_measurement_types_t measurement_type = {0};

    parse_arguments(argc, argv, &measurement_type);

    status = hailo_scan_devices(NULL, device_ids, &actual_device_count);
    REQUIRE_SUCCESS(status, l_exit, "Failed to scan devices");

    status = hailo_init_vdevice_params(&params);
    REQUIRE_SUCCESS(status, l_exit, "Failed to init vdevice_params");

    /* Scheduler over multiple devices is currently not supported */
    params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;

    params.device_count = (uint32_t)actual_device_count;
    status = hailo_create_vdevice(&params, &vdevice);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create vdevice");

    status = hailo_get_physical_devices(vdevice, physical_devices, &actual_device_count);
    REQUIRE_SUCCESS(status, l_release_vdevice, "Failed to get physical devices");

    for (size_t i = 0; i < actual_device_count; i++) {
        status = hailo_stop_power_measurement(physical_devices[i]);
        REQUIRE_SUCCESS(status, l_exit, "Failed stopping former measurements");

        status = hailo_set_power_measurement(physical_devices[i], MEASUREMENT_BUFFER_INDEX, DVM_OPTION, measurement_type);
        REQUIRE_SUCCESS(status, l_exit, "Failed setting measurement params");

        status = hailo_start_power_measurement(physical_devices[i], AVERAGE_FACTOR, SAMPLING_PERIOD);
        REQUIRE_SUCCESS(status, l_exit, "Failed to start measurement");
    }

    sleep_seconds(MEASUREMENTS_DURATION_SECS);

    for (size_t i = 0; i < actual_device_count; i++) {
        status = hailo_stop_power_measurement(physical_devices[i]);
        REQUIRE_SUCCESS(status, l_exit, "Failed to stop measurement");

        status = hailo_get_power_measurement(physical_devices[i], MEASUREMENT_BUFFER_INDEX, true, &(measurement_result[i]));
        REQUIRE_SUCCESS(status, l_exit, "Failed to get measurement results");

        status = print_measurements_results(physical_devices[i], &(measurement_result[i]), measurement_type);
        REQUIRE_SUCCESS(status, l_release_vdevice, "Failed to print measurement results");
    }

    status = HAILO_SUCCESS;

l_release_vdevice:
    (void) hailo_release_vdevice(vdevice);
l_exit:
    return (int)status;
}