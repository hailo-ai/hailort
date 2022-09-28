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

enum input_run_once_handle_t {
    INPUT_RUN_ONCE_HANDLE__FLUSH,
    INPUT_RUN_ONCE_HANDLE__ABORT,
    INPUT_RUN_ONCE_HANDLE__CLEAR_ABORT
};

enum output_run_once_handle_t {
    OUTPUT_RUN_ONCE_HANDLE__ABORT,
    OUTPUT_RUN_ONCE_HANDLE__CLEAR_ABORT
};

class VDeviceInputStreamWrapper : public InputStreamBase {
public:
    virtual ~VDeviceInputStreamWrapper() = default;
    VDeviceInputStreamWrapper(const VDeviceInputStreamWrapper &other) = delete;
    VDeviceInputStreamWrapper &operator=(const VDeviceInputStreamWrapper &other) = delete;
    VDeviceInputStreamWrapper &operator=(VDeviceInputStreamWrapper &&other) = delete;
    VDeviceInputStreamWrapper(VDeviceInputStreamWrapper &&other) = default;

    static Expected<std::unique_ptr<VDeviceInputStreamWrapper>> create(std::shared_ptr<VDeviceInputStream> vdevice_input_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
        multiplexer_ng_handle_t network_group_multiplexer_handle = 0);
    Expected<std::unique_ptr<VDeviceInputStreamWrapper>> clone(multiplexer_ng_handle_t network_group_multiplexer_handle);

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
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;

private:
    VDeviceInputStreamWrapper(std::shared_ptr<VDeviceInputStream> &vdevice_input_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
        multiplexer_ng_handle_t network_group_multiplexer_handle, hailo_status &status);

    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status flush() override;
    
    std::shared_ptr<VDeviceInputStream> m_vdevice_input_stream;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    scheduler_ng_handle_t m_network_group_scheduler_handle;
    multiplexer_ng_handle_t m_network_group_multiplexer_handle;
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
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
        multiplexer_ng_handle_t network_group_multiplexer_handle = 0);
    Expected<std::unique_ptr<VDeviceOutputStreamWrapper>> clone(multiplexer_ng_handle_t network_group_multiplexer_handle);

    virtual const hailo_stream_info_t &get_info() const override;
    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config() override;
    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;
    virtual Expected<size_t> get_buffer_frames_size() const override;
    virtual Expected<size_t> get_pending_frames_count() const override;

    virtual hailo_status register_for_d2h_interrupts(const std::function<void(uint32_t)> &callback) override
    {
        return m_vdevice_output_stream->register_for_d2h_interrupts(callback);
    }

protected:
    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer) override;

private:
    VDeviceOutputStreamWrapper(std::shared_ptr<VDeviceOutputStream> &vdevice_output_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
        multiplexer_ng_handle_t network_group_multiplexer_handle, hailo_status &status);

    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status read_all(MemoryView &buffer) override;
    virtual hailo_status read(MemoryView buffer) override;

    std::shared_ptr<VDeviceOutputStream> m_vdevice_output_stream;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    scheduler_ng_handle_t m_network_group_scheduler_handle;
    multiplexer_ng_handle_t m_network_group_multiplexer_handle;
    std::string m_network_name;
    EventPtr m_read_event;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_STREAM_WRAPPER_HPP_ */
