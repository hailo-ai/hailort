/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file notification_callback_example.c
 * This example demonstrates the basic usage of notification callbacks.
 * The program creates a device and then sets and removes a notification callback on it.
 * In this example the notification is HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM and the callback is a simple print function.
 **/

#include "common.h"
#include "hailo_thread.h"
#include "hailo/hailort.h"

#define DEVICE_IDS_COUNT (16)

void sleep_seconds(uint32_t duration_seconds)
{
#if defined(__unix__) || defined(__QNX__)
    sleep(duration_seconds);
#else
    Sleep(duration_seconds);
#endif
}

void callback(hailo_device device, const hailo_notification_t *notification, void *opaque)
{
    hailo_device_id_t device_id = {0};
    hailo_status status = hailo_get_device_id(device, &device_id);
    if (HAILO_SUCCESS != status){
        printf("Couldn't get device id\n");
        return;
    }
    printf("got a notification with notification id %d - Overcurrent Alarm\n", notification->id);
    printf("device id: %s\n", device_id.id);
    if(NULL == opaque)
        printf("User defined data is null\n");
}

int main()
{
    hailo_device_id_t device_ids[DEVICE_IDS_COUNT];
    size_t actual_devices_count = DEVICE_IDS_COUNT;
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_device device = NULL;

    // Scan to find a device
    status = hailo_scan_devices(NULL, device_ids, &actual_devices_count);
    REQUIRE_SUCCESS(status, l_exit, "Failed to scan devices");
    REQUIRE_ACTION(1 <= actual_devices_count, status = HAILO_INVALID_OPERATION, l_exit,
        "Failed to find a connected hailo device.");

    // Create the device
    status = hailo_create_device_by_id(&(device_ids[0]), &device);
    REQUIRE_SUCCESS(status, l_exit, "Failed to create device");

    // Set the callback function to the notification id HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM
    hailo_notification_callback callback_func = &callback;
    status = hailo_set_notification_callback(device, callback_func, HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM, NULL);
    REQUIRE_SUCCESS(status, l_release_device, "Failed to set notification callback");

    // In this part of the program - in case of HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM notification, the callback function will be called.
    printf("Notification callback has been set - ");
    printf("in case of overcurrent alarm notification, an overcurrent alarm message will be printed\n");
    sleep_seconds(2);

    // Remove the callback notification
    status = hailo_remove_notification_callback(device, HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM);
    REQUIRE_SUCCESS(status, l_release_device, "Failed to remove notification callback");
    printf("Notification callback has been removed\n");

l_release_device:
    (void) hailo_release_device(device);
l_exit:
    return (int)status;
}
