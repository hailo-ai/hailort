/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file notification_callback_example.cpp
 * This example demonstrates the basic usage of notification callbacks.
 * The program creates a device and then sets and removes a notification callback on it.
 * In this example the notification is HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM and the callback is a simple print function.
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <string>
#include <chrono>
#include <thread>


const std::chrono::seconds SLEEP_DURATION_SECS(2);

using namespace hailort;

int main()
{
    auto device_ids = Device::scan();
    if (!device_ids) {
        std::cerr << "Failed to scan, status = " << device_ids.status() << std::endl;
        return device_ids.status();
    }
    if (device_ids->size() < 1){
        std::cerr << "Failed to find a connected hailo device." << std::endl;
        return HAILO_INVALID_OPERATION;
    }
    auto device = Device::create(device_ids->at(0));
    if (!device) {
        std::cerr << "Failed to create device " << device.status() << std::endl;
        return device.status();
    }

    // Set the callback notification
    hailo_status status = device.value()->set_notification_callback(
    [] (Device &device, const hailo_notification_t &notification, void* opaque) {
        std::cout << "got notification with notification id " << notification.id << " - Overcurrent Alarm" << std::endl;
        std::cout << "device id: " << device.get_dev_id() << std::endl;
        if(nullptr == opaque)
            std::cout << "User defined data is null" << std::endl;
    },
    HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM, nullptr);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Setting notification failed "  << status << std::endl;
        return status;
    }

    std::cout << "Notification callback has been set - ";
    std::cout << "in case of overcurrent alarm notification, an overcurrent alarm will be printed" << std::endl;
    std::this_thread::sleep_for(SLEEP_DURATION_SECS);

    // Remove the callback notification
    status = device.value()->remove_notification_callback(HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Removing notification failed "  << status << std::endl;
        return status;
    }
    std::cout << "Notification callback has been removed" << std::endl;

    return HAILO_SUCCESS;
}