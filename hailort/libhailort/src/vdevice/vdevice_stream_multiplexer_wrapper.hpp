/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_stream_multiplexer_wrapper.hpp
 * @brief Wrapper classes for VDeviceInputStream and VDeviceOutputStream
 **/

#ifndef HAILO_VDEVICE_STREAM_MULTIPLEXER_WRAPPER_HPP_
#define HAILO_VDEVICE_STREAM_MULTIPLEXER_WRAPPER_HPP_

#include "hailo/expected.hpp"

#include "stream_common/stream_internal.hpp"
#include "vdevice/scheduler/scheduled_stream.hpp"
#include "vdevice/pipeline_multiplexer.hpp"


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

class VDeviceInputStreamMultiplexerWrapper : public InputStreamBase {
public:
    virtual ~VDeviceInputStreamMultiplexerWrapper() = default;
    static Expected<std::unique_ptr<VDeviceInputStreamMultiplexerWrapper>> create(
        std::shared_ptr<ScheduledInputStream> base_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
        multiplexer_core_op_handle_t core_op_multiplexer_handle = 0);
    Expected<std::unique_ptr<VDeviceInputStreamMultiplexerWrapper>> clone(multiplexer_core_op_handle_t core_op_multiplexer_handle);

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override;
    virtual const hailo_stream_info_t &get_info() const override;
    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config() override;
    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;

    virtual hailo_status launch_transfer(const device_id_t &device_id) override;
    virtual Expected<size_t> get_buffer_frames_size() const override;

protected:
    virtual hailo_status write_impl(const MemoryView &buffer) override;

private:
    VDeviceInputStreamMultiplexerWrapper(std::shared_ptr<ScheduledInputStream> base_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
        multiplexer_core_op_handle_t core_op_multiplexer_handle, hailo_status &status);

    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status flush() override;

    std::shared_ptr<ScheduledInputStream> m_base_stream;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    multiplexer_core_op_handle_t m_core_op_multiplexer_handle;
    std::string m_network_name;

    std::unique_ptr<std::atomic_bool> m_is_aborted;
};

class VDeviceOutputStreamMultiplexerWrapper : public OutputStreamBase {
public:
    virtual ~VDeviceOutputStreamMultiplexerWrapper() noexcept = default;

    static Expected<std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper>> create(
        std::shared_ptr<OutputStreamBase> base_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
        multiplexer_core_op_handle_t core_op_multiplexer_handle = 0);
    Expected<std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper>> clone(multiplexer_core_op_handle_t core_op_multiplexer_handle);

    virtual hailo_status set_buffer_mode(StreamBufferMode buffer_mode) override;
    virtual const hailo_stream_info_t &get_info() const override;
    virtual const CONTROL_PROTOCOL__nn_stream_config_t &get_nn_stream_config() override;
    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override;
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status launch_transfer(const device_id_t &device_id) override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override;
    virtual Expected<size_t> get_buffer_frames_size() const override;

private:
    VDeviceOutputStreamMultiplexerWrapper(std::shared_ptr<OutputStreamBase> base_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
        multiplexer_core_op_handle_t core_op_multiplexer_handle, hailo_status &status);

    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) override;
    virtual hailo_status read_impl(MemoryView buffer) override;

    std::shared_ptr<OutputStreamBase> m_base_stream;
    std::shared_ptr<PipelineMultiplexer> m_multiplexer;
    multiplexer_core_op_handle_t m_core_op_multiplexer_handle;
    std::string m_network_name;
    EventPtr m_read_event;

    std::unique_ptr<std::atomic_bool> m_is_aborted;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_STREAM_MULTIPLEXER_WRAPPER_HPP_ */
