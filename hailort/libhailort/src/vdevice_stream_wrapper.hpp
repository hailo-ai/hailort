/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream_wrapper.hpp
 * @brief Wrapper classes for VDeviceInputStream and VDeviceOutputStream
 **/

#ifndef HAILO_VDEVICE_STREAM_WRAPPER_HPP_
#define HAILO_VDEVICE_STREAM_WRAPPER_HPP_

#include "vdevice_stream.hpp"
#include "stream_internal.hpp"
#include "hailo/expected.hpp"
#include "context_switch/pipeline_multiplexer.hpp"

namespace hailort
{

class VDeviceInputStreamWrapper : public InputStreamBase {
public:
    virtual ~VDeviceInputStreamWrapper() = default;
    VDeviceInputStreamWrapper(const VDeviceInputStreamWrapper &other) = delete;
    VDeviceInputStreamWrapper &operator=(const VDeviceInputStreamWrapper &other) = delete;
    VDeviceInputStreamWrapper &operator=(VDeviceInputStreamWrapper &&other) = delete;
    VDeviceInputStreamWrapper(VDeviceInputStreamWrapper &&other) = default;

    static Expected<std::unique_ptr<VDeviceInputStreamWrapper>> create(std::shared_ptr<VDeviceInputStream> vdevice_input_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, network_group_handle_t network_group_handle);
    Expected<std::unique_ptr<VDeviceInputStreamWrapper>> clone(network_group_handle_t network_group_handle);

    virtual const hailo_stream_info_t &get_info() const override;
    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config() override;
    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;

    virtual Expected<PendingBufferState> send_pending_buffer() override;

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;

private:
    VDeviceInputStreamWrapper(std::shared_ptr<VDeviceInputStream> &vdevice_input_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, network_group_handle_t network_group_handle);

    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status flush() override;
    
    std::shared_ptr<VDeviceInputStream> m_vdevice_input_stream;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    network_group_handle_t m_network_group_handle;
    std::string m_network_name;
};

class VDeviceOutputStreamWrapper : public OutputStreamBase {
public:
    virtual ~VDeviceOutputStreamWrapper() noexcept = default;
    VDeviceOutputStreamWrapper(const VDeviceOutputStreamWrapper &other) = delete;
    VDeviceOutputStreamWrapper &operator=(const VDeviceOutputStreamWrapper &other) = delete;
    VDeviceOutputStreamWrapper &operator=(VDeviceOutputStreamWrapper &&other) = delete;
    VDeviceOutputStreamWrapper(VDeviceOutputStreamWrapper &&other) = default;

    static Expected<std::unique_ptr<VDeviceOutputStreamWrapper>> create(std::shared_ptr<VDeviceOutputStream> vdevice_output_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, network_group_handle_t network_group_handle);
    Expected<std::unique_ptr<VDeviceOutputStreamWrapper>> clone(network_group_handle_t network_group_handle);

    virtual const hailo_stream_info_t &get_info() const override;
    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config() override;
    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;

protected:
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer) override;

private:
    VDeviceOutputStreamWrapper(std::shared_ptr<VDeviceOutputStream> &vdevice_output_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, uint32_t id);

    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status read_all(MemoryView &buffer) override;
    virtual hailo_status read(MemoryView buffer) override;

    std::shared_ptr<VDeviceOutputStream> m_vdevice_output_stream;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    network_group_handle_t m_network_group_handle;
    std::string m_network_name;
    EventPtr m_read_event;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_STREAM_WRAPPER_HPP_ */
