#include "vdevice_stream_wrapper.hpp"

namespace hailort
{

const hailo_stream_info_t &VDeviceInputStreamWrapper::get_info() const
{
    return m_vdevice_input_stream->get_info();
}

const CONTROL_PROTOCOL__nn_stream_config_t &VDeviceInputStreamWrapper::get_nn_stream_config()
{
    return m_vdevice_input_stream->get_nn_stream_config();
}

hailo_status VDeviceInputStreamWrapper::activate_stream(uint16_t dynamic_batch_size)
{
    return m_vdevice_input_stream->activate_stream(dynamic_batch_size);
}

hailo_status VDeviceInputStreamWrapper::deactivate_stream()
{
    return m_vdevice_input_stream->deactivate_stream();
}

hailo_stream_interface_t VDeviceInputStreamWrapper::get_interface() const
{
    return m_vdevice_input_stream->get_interface();
}

std::chrono::milliseconds VDeviceInputStreamWrapper::get_timeout() const
{
    return m_vdevice_input_stream->get_timeout();
}

hailo_status VDeviceInputStreamWrapper::abort()
{
    return m_vdevice_input_stream->abort_impl(m_network_group_handle);
}

hailo_status VDeviceInputStreamWrapper::clear_abort()
{
    return m_vdevice_input_stream->clear_abort_impl(m_network_group_handle);
}

bool VDeviceInputStreamWrapper::is_scheduled()
{
    return m_vdevice_input_stream->is_scheduled();
}

Expected<PendingBufferState> VDeviceInputStreamWrapper::send_pending_buffer()
{
    return m_vdevice_input_stream->send_pending_buffer();
}

Expected<size_t> VDeviceInputStreamWrapper::sync_write_raw_buffer(const MemoryView &buffer)
{
    auto lock_exp = m_multiplexer->acquire_write_lock(m_network_group_handle);
    CHECK_AS_EXPECTED(HAILO_SUCCESS == lock_exp.status() || HAILO_NOT_FOUND == lock_exp.status(), lock_exp.status());
    auto exp = m_vdevice_input_stream->sync_write_raw_buffer_impl(buffer, m_network_group_handle);
    if (HAILO_STREAM_INTERNAL_ABORT == exp.status()) {
        return make_unexpected(exp.status());
    }
    CHECK_EXPECTED(exp);
    auto status = m_multiplexer->signal_sent_frame(m_network_group_handle, m_network_name);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return exp;
}

hailo_status VDeviceInputStreamWrapper::sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size)
{
    ASSERT(NULL != buffer);

    return sync_write_raw_buffer(MemoryView(static_cast<uint8_t*>(buffer) + offset, size)).status();
}

hailo_status VDeviceInputStreamWrapper::set_timeout(std::chrono::milliseconds timeout)
{
    return m_vdevice_input_stream->set_timeout(timeout);
}

hailo_status VDeviceInputStreamWrapper::flush()
{
    return m_vdevice_input_stream->flush();
}

Expected<std::unique_ptr<VDeviceInputStreamWrapper>> VDeviceInputStreamWrapper::create(std::shared_ptr<VDeviceInputStream> vdevice_input_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, network_group_handle_t network_group_handle)
{
    std::unique_ptr<VDeviceInputStreamWrapper> wrapper(new (std::nothrow) VDeviceInputStreamWrapper(vdevice_input_stream, network_name, multiplexer, network_group_handle));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceInputStreamWrapper>> VDeviceInputStreamWrapper::clone(network_group_handle_t network_group_handle)
{
    auto wrapper = create(m_vdevice_input_stream, m_network_name, m_multiplexer, network_group_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceInputStreamWrapper::VDeviceInputStreamWrapper(std::shared_ptr<VDeviceInputStream> &vdevice_input_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, network_group_handle_t network_group_handle) :
    InputStreamBase(vdevice_input_stream->get_info(),
        vdevice_input_stream->m_nn_stream_config, vdevice_input_stream->get_network_group_activated_event()),
    m_vdevice_input_stream(vdevice_input_stream),
    m_multiplexer(multiplexer),
    m_network_group_handle(network_group_handle),
    m_network_name(network_name)
{}

const hailo_stream_info_t &VDeviceOutputStreamWrapper::get_info() const
{
    return m_vdevice_output_stream->get_info();
}

const CONTROL_PROTOCOL__nn_stream_config_t &VDeviceOutputStreamWrapper::get_nn_stream_config()
{
    return m_vdevice_output_stream->get_nn_stream_config();
}

hailo_status VDeviceOutputStreamWrapper::activate_stream(uint16_t dynamic_batch_size)
{
    return m_vdevice_output_stream->activate_stream(dynamic_batch_size);
}

hailo_status VDeviceOutputStreamWrapper::deactivate_stream()
{
    return m_vdevice_output_stream->deactivate_stream();
}

hailo_stream_interface_t VDeviceOutputStreamWrapper::get_interface() const
{
    return m_vdevice_output_stream->get_interface();
}

std::chrono::milliseconds VDeviceOutputStreamWrapper::get_timeout() const
{
    return m_vdevice_output_stream->get_timeout();
}

hailo_status VDeviceOutputStreamWrapper::abort()
{
    return m_vdevice_output_stream->abort_impl(m_network_group_handle);
}

hailo_status VDeviceOutputStreamWrapper::clear_abort()
{
    return m_vdevice_output_stream->clear_abort_impl(m_network_group_handle);
}

bool VDeviceOutputStreamWrapper::is_scheduled()
{
    return m_vdevice_output_stream->is_scheduled();
}

Expected<size_t> VDeviceOutputStreamWrapper::sync_read_raw_buffer(MemoryView &buffer)
{
    return m_vdevice_output_stream->sync_read_raw_buffer(buffer);
}

hailo_status VDeviceOutputStreamWrapper::read_all(MemoryView &buffer)
{
    return m_vdevice_output_stream->read_all(buffer);
}

hailo_status VDeviceOutputStreamWrapper::read(MemoryView buffer)
{
    auto status = m_multiplexer->reader_wait(m_network_group_handle, m_network_name, get_info().name);
    CHECK_SUCCESS(status);
    status = m_vdevice_output_stream->read_impl(buffer, m_network_group_handle);
    if (HAILO_STREAM_INTERNAL_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);
    return m_multiplexer->signal_received_frame(m_network_group_handle, m_network_name, get_info().name);
}

hailo_status VDeviceOutputStreamWrapper::set_timeout(std::chrono::milliseconds timeout)
{
    return m_vdevice_output_stream->set_timeout(timeout);
}

Expected<std::unique_ptr<VDeviceOutputStreamWrapper>> VDeviceOutputStreamWrapper::create(std::shared_ptr<VDeviceOutputStream> vdevice_output_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, network_group_handle_t network_group_handle)
{
    std::unique_ptr<VDeviceOutputStreamWrapper> wrapper(new (std::nothrow) VDeviceOutputStreamWrapper(vdevice_output_stream, network_name, multiplexer, network_group_handle));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceOutputStreamWrapper>> VDeviceOutputStreamWrapper::clone(network_group_handle_t network_group_handle)
{
    auto wrapper = create(m_vdevice_output_stream, m_network_name, m_multiplexer, network_group_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceOutputStreamWrapper::VDeviceOutputStreamWrapper(std::shared_ptr<VDeviceOutputStream> &vdevice_output_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, network_group_handle_t network_group_handle) :
    OutputStreamBase(vdevice_output_stream->get_layer_info(), vdevice_output_stream->get_info(),
        vdevice_output_stream->m_nn_stream_config, vdevice_output_stream->get_network_group_activated_event()),
    m_vdevice_output_stream(vdevice_output_stream),
    m_multiplexer(multiplexer),
    m_network_group_handle(network_group_handle),
    m_network_name(network_name)
{}

} /* namespace hailort */
