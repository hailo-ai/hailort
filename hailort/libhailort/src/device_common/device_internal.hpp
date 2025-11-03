/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_internal.hpp
 * @brief Class declaration for DeviceBase that implements the basic Device "interface" (not technically
 *        an interface, but good enough). All internal devices should inherit from the DeviceBase class.
 *        Hence, the hierarchy is as follows:
 *
 * Device                       (External "interface")
 * |-- BaseDevice               (Base classes)
 *     |-- VdmaDevice
 *     |   |-- PcieDevice
 *     |   |-- IntegratedDevice
 * |-- DeviceHrpcClient     (RPC handle communicating with the server)
 **/

#ifndef _HAILO_DEVICE_INTERNAL_HPP_
#define _HAILO_DEVICE_INTERNAL_HPP_

#include "hailo/device.hpp"
#include "hailo/hailort.h"

#include "d2h_event_queue.hpp"

#include "firmware_header.h"
#include "firmware_header_utils.h"
#include "control_protocol.h"
#include <thread>


namespace hailort
{

#define EVB_PART_NUMBER_PREFIX ("HEV18B1C4GA")
#define MDOT2_PART_NUMBER_PREFIX ("HM218B1C2FA")
#define MPCIE_PART_NUMBER_PREFIX ("HMP1RB1C2GA")

// Will be used to perfrom generic validation for all variations of a specific module
#define PART_NUMBER_PREFIX_LENGTH (11)

#define CLOCKS_IN_MHZ (1000 * 1000)

enum class HEFHwArch // Must be aligned to ProtoHEFHwArch
{
    HW_ARCH__HAILO8 = 0,
    HW_ARCH__HAILO8P = 1,
    HW_ARCH__HAILO8R = 2,
    HW_ARCH__HAILO8L = 3,
    HW_ARCH__HAILO1XH = 103,
    HW_ARCH__HAILO15M = 4,
    //HW_ARCH__HAILO10H = 5, // Deprecated
    HW_ARCH__HAILO15L = 6,

    HW_ARCH__SAGE_A0 = 100,
    HW_ARCH__SAGE_B0 = 101,
    HW_ARCH__PAPRIKA_B0 = 102,
    HW_ARCH__GINGER = 104,
    HW_ARCH__LAVENDER = 105,
    HW_ARCH__PLUTO = 106,
    HW_ARCH__MARS = 108,
};

class DeviceBase : public Device
{
public:
    DeviceBase(Type type);
    DeviceBase(DeviceBase &&) = delete;
    DeviceBase(const DeviceBase &) = delete;
    DeviceBase &operator=(DeviceBase &&) = delete;
    DeviceBase &operator=(const DeviceBase &) = delete;
    virtual ~DeviceBase();

    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &hef,
        const NetworkGroupsParamsMap &configure_params={}) override;
    virtual hailo_status reset(hailo_reset_device_mode_t mode) override;
    virtual hailo_status set_notification_callback(const NotificationCallback &func, hailo_notification_id_t notification_id, void *opaque) override;
    virtual hailo_status remove_notification_callback(hailo_notification_id_t notification_id) override;
    virtual void activate_notifications(const std::string &device_id);
    virtual void start_notification_fetch_thread(D2hEventQueue *write_queue);
    virtual hailo_status stop_notification_fetch_thread();
    virtual hailo_status firmware_update(const MemoryView &firmware_binary, bool should_reset) override;
    virtual hailo_status second_stage_update(uint8_t *second_stage_binary, uint32_t second_stage_binary_length) override;
    virtual hailo_status store_sensor_config(uint32_t section_index, hailo_sensor_types_t sensor_type,
        uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
        const std::string &config_file_path, const std::string &config_name) override;
    virtual hailo_status store_isp_config(uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
        const std::string &isp_static_config_file_path, const std::string &isp_runtime_config_file_path, const std::string &config_name) override;
    virtual Expected<Buffer> sensor_get_sections_info() override;
    virtual hailo_status sensor_dump_config(uint32_t section_index, const std::string &config_file_path) override;
    virtual hailo_status sensor_set_i2c_bus_index(hailo_sensor_types_t sensor_type, uint32_t bus_index) override;
    virtual hailo_status sensor_load_and_start_config(uint32_t section_index) override;
    virtual hailo_status sensor_reset(uint32_t section_index) override;
    virtual hailo_status sensor_set_generic_i2c_slave(uint16_t slave_address, uint8_t offset_size, uint8_t bus_index,
        uint8_t should_hold_bus, uint8_t slave_endianness) override;
    virtual Expected<Buffer> read_board_config() override;
    virtual hailo_status write_board_config(const MemoryView &buffer) override;
    virtual Expected<hailo_fw_user_config_information_t> examine_user_config() override;
    virtual Expected<Buffer> read_user_config() override;
    virtual hailo_status write_user_config(const MemoryView &buffer) override;
    virtual hailo_status erase_user_config() override;
    static std::vector<hailo_device_architecture_t> hef_arch_to_device_compatible_archs(HEFHwArch hef_arch);

    virtual Expected<size_t> fetch_logs(MemoryView buffer, hailo_log_type_t log_type) override;

    virtual Expected<hailo_device_architecture_t> get_architecture() const override
    {
        // FW is always up if we got here (device implementations's ctor would fail otherwise)
        // Hence, just return it
        return Expected<hailo_device_architecture_t>(m_device_architecture);
    }

    virtual hailo_status before_fork() override
    {
        return HAILO_SUCCESS;
    }

    virtual hailo_status after_fork_in_parent() override
    {
        return HAILO_SUCCESS;
    }

    virtual hailo_status after_fork_in_child() override
    {
        return HAILO_SUCCESS;
    }

    virtual hailo_status echo_buffer_async(const MemoryView buffer) override
    {
        (void)buffer;
        return HAILO_NOT_IMPLEMENTED;
    }

protected:
    struct NotificationThreadSharedParams {
        NotificationThreadSharedParams() : is_running(false) {}
        D2hEventQueue *write_queue;
        bool is_running;
    };

    // Special value to signal the d2h notification thread to terminate
    static const uint32_t TERMINATE_EVENT_ID = std::numeric_limits<uint32_t>::max();
    
    virtual void shutdown_core_ops() = 0;
    virtual hailo_reset_device_mode_t get_default_reset_mode() = 0;
    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) = 0;
    virtual Expected<D2H_EVENT_MESSAGE_t> read_notification() = 0;
    virtual hailo_status disable_notifications() = 0;
    void start_d2h_notification_thread(const std::string &device_id);
    void stop_d2h_notification_thread();
    void d2h_notification_thread_main(const std::string &device_id);
    hailo_status check_hef_is_compatible(Hef &hef);

    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef, const NetworkGroupsParamsMap &configure_params) = 0;
    
    D2hEventQueue m_d2h_notification_queue;
    std::thread m_d2h_notification_thread;
    std::thread m_notification_fetch_thread;
    std::shared_ptr<NotificationThreadSharedParams> m_notif_fetch_thread_params;

private:
    friend class VDeviceBase;

    static hailo_status fw_notification_id_to_hailo(D2H_EVENT_ID_t fw_notification_id,
        hailo_notification_id_t* hailo_notification_id);
    static hailo_status validate_binary_version_for_platform(firmware_version_t *new_binary_version, 
        firmware_version_t *min_supported_binary_version, FW_BINARY_TYPE_t fw_binary_type);
    static hailo_status validate_fw_version_for_platform(const hailo_device_identity_t &board_info,
        firmware_version_t fw_version, FW_BINARY_TYPE_t fw_binary_type);
    static void check_clock_rate_for_hailo8(uint32_t clock_rate, HEFHwArch hef_hw_arch);
    hailo_status store_sensor_control_buffers(const std::vector<SENSOR_CONFIG__operation_cfg_t> &control_buffers, uint32_t section_index, hailo_sensor_types_t sensor_type,
        uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps, const std::string &config_name);
    virtual void notification_fetch_thread(std::shared_ptr<NotificationThreadSharedParams> params);
    Expected<firmware_type_t> get_fw_type();

    typedef struct {
        std::shared_ptr<NotificationCallback> func;
        void *opaque;
    } d2h_notification_callback_t;

    d2h_notification_callback_t m_d2h_callbacks[HAILO_NOTIFICATION_ID_COUNT];
    std::mutex m_callbacks_lock;
    bool m_is_shutdown_core_ops_called;
};

} /* namespace hailort */

#endif /* _HAILO_DEVICE_INTERNAL_HPP_ */
