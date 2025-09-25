/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_device_hrpc_client.hpp
 * @brief Pcie Device HRPC client, represents the user's handle to the Device object (held in the hailort server)
 **/

#ifndef HAILO_PCIE_DEVICE_HRPC_CLIENT_HPP_
#define HAILO_PCIE_DEVICE_HRPC_CLIENT_HPP_

#include "hailo/device.hpp"
#include "hailo/hailort.h"
#include "hrpc/client.hpp"
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"


namespace hailort
{

class PcieDeviceHrpcClient : public Device {
public:
    static Expected<std::unique_ptr<PcieDeviceHrpcClient>> create(const std::string &device_id);
    static Expected<std::unique_ptr<PcieDeviceHrpcClient>> create(const std::string &device_id,
        std::shared_ptr<Client> client);

    PcieDeviceHrpcClient(const std::string &device_id, std::shared_ptr<Client> client, uint32_t handle,
        std::shared_ptr<ClientCallbackDispatcher> callback_dispatcher) :
        Device(Device::Type::PCIE), m_device_id(device_id), m_client(client), m_handle(handle), m_callback_dispatcher(callback_dispatcher) {}
    virtual ~PcieDeviceHrpcClient();

    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &/*hef*/,
        const NetworkGroupsParamsMap &configure_params={}) override { (void)configure_params; return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual Expected<size_t> read_log(MemoryView &/*buffer*/, hailo_cpu_id_t /*cpu_id*/) override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual hailo_status reset(hailo_reset_device_mode_t mode) override;
    virtual hailo_status firmware_update(const MemoryView &/*firmware_binary*/, bool /*should_reset*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status second_stage_update(uint8_t */*second_stage_binary*/, uint32_t /*second_stage_binary_length*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status store_sensor_config(uint32_t /*section_index*/, hailo_sensor_types_t /*sensor_type*/,
        uint32_t /*reset_config_size*/, uint16_t /*config_height*/, uint16_t /*config_width*/, uint16_t /*config_fps*/,
        const std::string &/*config_file_path*/, const std::string &/*config_name*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status store_isp_config(uint32_t /*reset_config_size*/, uint16_t /*config_height*/, uint16_t /*config_width*/, uint16_t /*config_fps*/,
        const std::string &/*isp_static_config_file_path*/, const std::string &/*isp_runtime_config_file_path*/, const std::string &/*config_name*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual Expected<Buffer> sensor_get_sections_info() override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual hailo_status sensor_dump_config(uint32_t /*section_index*/, const std::string &/*config_file_path*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status sensor_set_i2c_bus_index(hailo_sensor_types_t /*sensor_type*/, uint32_t /*bus_index*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status sensor_load_and_start_config(uint32_t /*section_index*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status sensor_reset(uint32_t /*section_index*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status sensor_set_generic_i2c_slave(uint16_t /*slave_address*/, uint8_t /*offset_size*/, uint8_t /*bus_index*/,
        uint8_t /*should_hold_bus*/, uint8_t /*slave_endianness*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual Expected<Buffer> read_board_config() override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual hailo_status write_board_config(const MemoryView &/*buffer*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual Expected<hailo_fw_user_config_information_t> examine_user_config() override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual Expected<Buffer> read_user_config() override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual hailo_status write_user_config(const MemoryView &/*buffer*/) override { return HAILO_NOT_IMPLEMENTED; }
    virtual hailo_status erase_user_config() override { return HAILO_NOT_IMPLEMENTED; }
    virtual const char* get_dev_id() const override { return m_device_id.c_str(); }
    virtual bool is_stream_interface_supported(const hailo_stream_interface_t &/*stream_interface*/) const override { return false; }

    virtual hailo_status wait_for_wakeup() override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual void increment_control_sequence() override {}
    virtual hailo_status fw_interact_impl(uint8_t */*request_buffer*/, size_t /*request_size*/, uint8_t */*response_buffer*/, 
                                          size_t */*response_size*/, hailo_cpu_id_t /*cpu_id*/) override { return HAILO_NOT_IMPLEMENTED; }

    virtual Expected<hailo_device_identity_t> identify() override;
    virtual Expected<hailo_extended_device_information_t> get_extended_device_information() override;
    virtual Expected<hailo_chip_temperature_info_t> get_chip_temperature() override;
    virtual Expected<hailo_health_stats_t> query_health_stats() override;
    virtual Expected<hailo_performance_stats_t> query_performance_stats() override;
    virtual Expected<float32_t> power_measurement(hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type) override;
    virtual hailo_status start_power_measurement(hailo_averaging_factor_t averaging_factor, hailo_sampling_period_t sampling_period) override;
    virtual Expected<hailo_power_measurement_data_t> get_power_measurement(hailo_measurement_buffer_index_t buffer_index, bool should_clear) override;
    virtual hailo_status set_power_measurement(hailo_measurement_buffer_index_t buffer_index, hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type) override;
    virtual hailo_status stop_power_measurement() override;
    virtual Expected<hailo_device_architecture_t> get_architecture() const override;
    virtual hailo_status set_notification_callback(const NotificationCallback &func, hailo_notification_id_t notification_id,
        void *opaque) override;
    virtual hailo_status remove_notification_callback(hailo_notification_id_t notification_id) override;

    virtual hailo_status dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;
    virtual hailo_status dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction) override;

    virtual hailo_status before_fork() override;
    virtual hailo_status after_fork_in_parent() override;
    virtual hailo_status after_fork_in_child() override;

private:
    static Expected<std::shared_ptr<Client>> get_connected_client(const std::string &device_id);
    static Expected<rpc_object_handle_t> create_remote_device(std::shared_ptr<Client> client);

    std::string m_device_id;
    std::shared_ptr<Client> m_client;
    uint32_t m_handle;
    std::shared_ptr<ClientCallbackDispatcher> m_callback_dispatcher;
};

} /* namespace hailort */

#endif /* HAILO_PCIE_DEVICE_HRPC_CLIENT_HPP_ */
