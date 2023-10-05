#include "vdevice/vdevice_stream_multiplexer_wrapper.hpp"

namespace hailort
{

hailo_status VDeviceInputStreamMultiplexerWrapper::set_buffer_mode(StreamBufferMode buffer_mode)
{
    // Buffer is not owned by this class, so we just forward the request to base stream.
    return m_base_stream->set_buffer_mode(buffer_mode);
}

const hailo_stream_info_t &VDeviceInputStreamMultiplexerWrapper::get_info() const
{
    return m_base_stream->get_info();
}

const CONTROL_PROTOCOL__nn_stream_config_t &VDeviceInputStreamMultiplexerWrapper::get_nn_stream_config()
{
    return m_base_stream->get_nn_stream_config();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::activate_stream()
{
    return m_base_stream->activate_stream();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::deactivate_stream()
{
    return m_base_stream->deactivate_stream();
}

hailo_stream_interface_t VDeviceInputStreamMultiplexerWrapper::get_interface() const
{
    return m_base_stream->get_interface();
}

std::chrono::milliseconds VDeviceInputStreamMultiplexerWrapper::get_timeout() const
{
    return m_base_stream->get_timeout();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::abort()
{
    if (*m_is_aborted) {
        return HAILO_SUCCESS;
    }
    *m_is_aborted = true;

    auto status = m_multiplexer->disable_stream(m_core_op_multiplexer_handle, name());
    CHECK_SUCCESS(status);

    m_base_stream->notify_all();

    status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__ABORT, m_core_op_multiplexer_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceInputStreamMultiplexerWrapper::clear_abort()
{
    if (!(*m_is_aborted)) {
        return HAILO_SUCCESS;
    }
    *m_is_aborted = false;

    auto status = m_multiplexer->enable_stream(m_core_op_multiplexer_handle, name());
    CHECK_SUCCESS(status);

    status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, m_core_op_multiplexer_handle);
    CHECK_SUCCESS(status);

    m_base_stream->notify_all();

    return HAILO_SUCCESS;
}

bool VDeviceInputStreamMultiplexerWrapper::is_scheduled()
{
    // Multiplexer can only work with scheduler
    assert(m_base_stream->is_scheduled());
    return true;
}

hailo_status VDeviceInputStreamMultiplexerWrapper::launch_transfer(const device_id_t &device_id)
{
    return m_base_stream->launch_transfer(device_id);
}

Expected<size_t> VDeviceInputStreamMultiplexerWrapper::get_buffer_frames_size() const
{
    return m_base_stream->get_buffer_frames_size();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::write_impl(const MemoryView &buffer)
{
    auto status = m_multiplexer->wait_for_write(m_core_op_multiplexer_handle);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    auto write_status = m_base_stream->write_impl(buffer, [this]() { return m_is_aborted->load(); });
    status = m_multiplexer->signal_write_finish(m_core_op_multiplexer_handle, write_status != HAILO_SUCCESS);
    CHECK_SUCCESS(status);
    if (HAILO_STREAM_ABORTED_BY_USER == write_status) {
        return write_status;
    }
    CHECK_SUCCESS(write_status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceInputStreamMultiplexerWrapper::set_timeout(std::chrono::milliseconds timeout)
{
    return m_base_stream->set_timeout(timeout);
}

hailo_status VDeviceInputStreamMultiplexerWrapper::flush()
{
    return m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__FLUSH, m_core_op_multiplexer_handle);
}

Expected<std::unique_ptr<VDeviceInputStreamMultiplexerWrapper>> VDeviceInputStreamMultiplexerWrapper::create(
    std::shared_ptr<ScheduledInputStream> base_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
    multiplexer_core_op_handle_t core_op_multiplexer_handle)
{
    assert(base_stream->is_scheduled());
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<VDeviceInputStreamMultiplexerWrapper> wrapper(
        new (std::nothrow) VDeviceInputStreamMultiplexerWrapper(base_stream, network_name, multiplexer,
            core_op_multiplexer_handle, status));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceInputStreamMultiplexerWrapper>> VDeviceInputStreamMultiplexerWrapper::clone(
    multiplexer_core_op_handle_t core_op_multiplexer_handle)
{
    auto wrapper = create(m_base_stream, m_network_name, m_multiplexer, core_op_multiplexer_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceInputStreamMultiplexerWrapper::VDeviceInputStreamMultiplexerWrapper(
    std::shared_ptr<ScheduledInputStream> base_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
    multiplexer_core_op_handle_t core_op_multiplexer_handle, hailo_status &status) :
    InputStreamBase(base_stream->get_layer_info(), base_stream->get_interface(),
        base_stream->get_core_op_activated_event(), status),
    m_base_stream(base_stream),
    m_multiplexer(multiplexer),
    m_core_op_multiplexer_handle(core_op_multiplexer_handle),
    m_network_name(network_name),
    m_is_aborted()
{
    if (HAILO_SUCCESS != status) {
        // Parent returned error
        return;
    }

    m_is_aborted = make_unique_nothrow<std::atomic_bool>(false);
    if (nullptr == m_is_aborted) {
        status = HAILO_OUT_OF_HOST_MEMORY;
        LOGGER__ERROR("Failed to allocate memory! status = {}", status);
        return;
    }
    status = multiplexer->register_run_once_for_stream(base_stream->name(), INPUT_RUN_ONCE_HANDLE__FLUSH, [this]
    {
        return m_base_stream->flush();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(base_stream->name(), INPUT_RUN_ONCE_HANDLE__ABORT, [this]
    {
        return m_base_stream->abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(base_stream->name(), INPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, [this]
    {
        return m_base_stream->clear_abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::set_buffer_mode(StreamBufferMode buffer_mode)
{
    // Buffer is not owned by this class, so we just forward the request to base stream.
    return m_base_stream->set_buffer_mode(buffer_mode);
}

const hailo_stream_info_t &VDeviceOutputStreamMultiplexerWrapper::get_info() const
{
    return m_base_stream->get_info();
}

const CONTROL_PROTOCOL__nn_stream_config_t &VDeviceOutputStreamMultiplexerWrapper::get_nn_stream_config()
{
    return m_base_stream->get_nn_stream_config();
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::activate_stream()
{
    return m_base_stream->activate_stream();
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::deactivate_stream()
{
    return m_base_stream->deactivate_stream();
}

hailo_stream_interface_t VDeviceOutputStreamMultiplexerWrapper::get_interface() const
{
    return m_base_stream->get_interface();
}

std::chrono::milliseconds VDeviceOutputStreamMultiplexerWrapper::get_timeout() const
{
    return m_base_stream->get_timeout();
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::launch_transfer(const device_id_t &device_id)
{
    return m_base_stream->launch_transfer(device_id);
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::abort()
{
    if (*m_is_aborted) {
        return HAILO_SUCCESS;
    }
    *m_is_aborted = true;

    auto status = m_multiplexer->disable_stream(m_core_op_multiplexer_handle, name());
    CHECK_SUCCESS(status);

    status = m_multiplexer->run_once_for_stream(name(), OUTPUT_RUN_ONCE_HANDLE__ABORT, m_core_op_multiplexer_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::clear_abort()
{
    if (!(*m_is_aborted)) {
        return HAILO_SUCCESS;
    }
    *m_is_aborted = false;

    auto status = m_multiplexer->enable_stream(m_core_op_multiplexer_handle, name());
    CHECK_SUCCESS(status);

    status = m_multiplexer->run_once_for_stream(name(), OUTPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, m_core_op_multiplexer_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool VDeviceOutputStreamMultiplexerWrapper::is_scheduled()
{
    // Multiplexer can only work with scheduler
    assert(m_base_stream->is_scheduled());
    return true;
}

Expected<size_t> VDeviceOutputStreamMultiplexerWrapper::get_buffer_frames_size() const
{
    return m_base_stream->get_buffer_frames_size();
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::read_impl(MemoryView buffer)
{
    uint32_t frames_to_drain_count = 0;
    auto expected_drain_count = m_multiplexer->wait_for_read(m_core_op_multiplexer_handle, name(),
        m_base_stream->get_timeout());
    if (HAILO_STREAM_ABORTED_BY_USER == expected_drain_count.status()) {
        return expected_drain_count.status();
    }
    CHECK_EXPECTED_AS_STATUS(expected_drain_count);

    frames_to_drain_count = expected_drain_count.release();

    for (uint32_t i = 0; i < frames_to_drain_count; i++) {
        auto status = m_base_stream->read(buffer);
        if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    auto status = m_base_stream->read(buffer);
    if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return status;
    }
    CHECK_SUCCESS(status);

    status = m_multiplexer->signal_read_finish();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::set_timeout(std::chrono::milliseconds timeout)
{
    return m_base_stream->set_timeout(timeout);
}

Expected<std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper>> VDeviceOutputStreamMultiplexerWrapper::create(
    std::shared_ptr<OutputStreamBase> base_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
    multiplexer_core_op_handle_t core_op_multiplexer_handle)
{
    assert(base_stream->is_scheduled());
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper> wrapper(
        new (std::nothrow) VDeviceOutputStreamMultiplexerWrapper(base_stream, network_name, multiplexer,
            core_op_multiplexer_handle, status));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper>> VDeviceOutputStreamMultiplexerWrapper::clone(
    multiplexer_core_op_handle_t core_op_multiplexer_handle)
{
    auto wrapper = create(m_base_stream, m_network_name, m_multiplexer, core_op_multiplexer_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceOutputStreamMultiplexerWrapper::VDeviceOutputStreamMultiplexerWrapper(
        std::shared_ptr<OutputStreamBase> base_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer,
        multiplexer_core_op_handle_t core_op_multiplexer_handle, hailo_status &status) :
    OutputStreamBase(base_stream->get_layer_info(), base_stream->get_info(),
        base_stream->m_nn_stream_config, base_stream->get_core_op_activated_event()),
    m_base_stream(base_stream),
    m_multiplexer(multiplexer),
    m_core_op_multiplexer_handle(core_op_multiplexer_handle),
    m_network_name(network_name),
    m_is_aborted()
{
    m_is_aborted = make_unique_nothrow<std::atomic_bool>(false);
    if (nullptr == m_is_aborted) {
        status = HAILO_OUT_OF_HOST_MEMORY;
        LOGGER__ERROR("Failed to allocate memory! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(m_base_stream->name(), OUTPUT_RUN_ONCE_HANDLE__ABORT, [this]
    {
        return m_base_stream->abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(m_base_stream->name(), OUTPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, [this]
    {
        return m_base_stream->clear_abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }
}

} /* namespace hailort */
