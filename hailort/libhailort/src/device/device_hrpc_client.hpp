/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_hrpc_client.hpp
 * @brief Device HRPC client, represents the user's handle to the Device object (held in the hailort server)
 **/

#ifndef HAILO_DEVICE_HRPC_CLIENT_HPP_
#define HAILO_DEVICE_HRPC_CLIENT_HPP_

#include "hailo/device.hpp"
#include "hailo/hailort.h"
#include "hrpc/client.hpp"
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"


namespace hailort
{

class DeviceHrpcClient : public Device {
public:
    static Expected<std::unique_ptr<Device>> create(const std::string &device_id);
    static Expected<std::unique_ptr<Device>> create(std::shared_ptr<Client> client);

    DeviceHrpcClient(std::shared_ptr<Client> client, uint32_t handle, std::shared_ptr<ClientCallbackDispatcher> callback_dispatcher) :
        Device(client->device_type()), m_device_id(client->device_id()), m_client(client), m_handle(handle), m_callback_dispatcher(callback_dispatcher) {}
    virtual ~DeviceHrpcClient();

    virtual Expected<ConfiguredNetworkGroupVector> configure(Hef &/*hef*/,
        const NetworkGroupsParamsMap &configure_params={}) override { (void)configure_params; return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual Expected<size_t> read_log(MemoryView &/*buffer*/, hailo_cpu_id_t /*cpu_id*/) override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual hailo_status reset(hailo_reset_device_mode_t mode) override;
    virtual Expected<Buffer> read_board_config() override { return make_unexpected(HAILO_NOT_IMPLEMENTED); }
    virtual hailo_status write_board_config(const MemoryView &/*buffer*/) override { return HAILO_NOT_IMPLEMENTED; }
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
    virtual Expected<hailo_performance_stats_t> query_performance_stats(std::chrono::milliseconds sampling_period = std::chrono::milliseconds(100)) override;
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
    virtual hailo_status echo_buffer_async(const MemoryView buffer) override;

    virtual Expected<bool> has_power_sensor() override;
    virtual Expected<uint32_t> get_current_limit() override;
    virtual Expected<size_t> fetch_logs(MemoryView buffer, hailo_log_type_t log_type) override;

private:
    static Expected<std::shared_ptr<Client>> create_connected_client(const std::string &device_id);
    static Expected<rpc_object_handle_t> create_remote_device(std::shared_ptr<Client> client);

    std::string m_device_id;
    std::shared_ptr<Client> m_client;
    uint32_t m_handle;
    std::shared_ptr<ClientCallbackDispatcher> m_callback_dispatcher;
};

} /* namespace hailort */

#endif /* HAILO_DEVICE_HRPC_CLIENT_HPP_ */
