#include "vdevice_stream_multiplexer_wrapper.hpp"

namespace hailort
{

const hailo_stream_info_t &VDeviceInputStreamMultiplexerWrapper::get_info() const
{
    return m_vdevice_input_stream->get_info();
}

const CONTROL_PROTOCOL__nn_stream_config_t &VDeviceInputStreamMultiplexerWrapper::get_nn_stream_config()
{
    return m_vdevice_input_stream->get_nn_stream_config();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::activate_stream(uint16_t dynamic_batch_size)
{
    return m_vdevice_input_stream->activate_stream(dynamic_batch_size);
}

hailo_status VDeviceInputStreamMultiplexerWrapper::deactivate_stream()
{
    return m_vdevice_input_stream->deactivate_stream();
}

hailo_stream_interface_t VDeviceInputStreamMultiplexerWrapper::get_interface() const
{
    return m_vdevice_input_stream->get_interface();
}

std::chrono::milliseconds VDeviceInputStreamMultiplexerWrapper::get_timeout() const
{
    return m_vdevice_input_stream->get_timeout();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::abort()
{
    if (is_scheduled()) {
        auto status = m_multiplexer->disable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        *m_is_aborted = true;
        m_vdevice_input_stream->notify_all();

        // TODO: HRT-7638
        status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_input_stream->abort();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceInputStreamMultiplexerWrapper::clear_abort()
{
    if (is_scheduled()) {
        auto status = m_multiplexer->enable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        *m_is_aborted = false;

        status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        m_vdevice_input_stream->notify_all();

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_input_stream->clear_abort();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool VDeviceInputStreamMultiplexerWrapper::is_scheduled()
{
    return m_vdevice_input_stream->is_scheduled();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::send_pending_buffer(size_t device_index)
{
    return m_vdevice_input_stream->send_pending_buffer(device_index);
}

Expected<size_t> VDeviceInputStreamMultiplexerWrapper::get_buffer_frames_size() const
{
    return m_vdevice_input_stream->get_buffer_frames_size();
}

Expected<size_t> VDeviceInputStreamMultiplexerWrapper::get_pending_frames_count() const
{
    return m_vdevice_input_stream->get_pending_frames_count();
}

Expected<size_t> VDeviceInputStreamMultiplexerWrapper::sync_write_raw_buffer(const MemoryView &buffer)
{
    if (is_scheduled()) {
        auto status = m_multiplexer->wait_for_write(m_network_group_multiplexer_handle);
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return make_unexpected(status);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto exp = m_vdevice_input_stream->sync_write_raw_buffer(buffer, [this]() { return m_is_aborted->load(); });
    if (HAILO_STREAM_ABORTED_BY_USER == exp.status()) {
        return make_unexpected(exp.status());
    }
    CHECK_EXPECTED(exp);

    if (is_scheduled()) {
        auto status = m_multiplexer->signal_write_finish(m_network_group_multiplexer_handle);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    return exp;
}

hailo_status VDeviceInputStreamMultiplexerWrapper::sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size)
{
    ASSERT(NULL != buffer);

    return sync_write_raw_buffer(MemoryView(static_cast<uint8_t*>(buffer) + offset, size)).status();
}

hailo_status VDeviceInputStreamMultiplexerWrapper::set_timeout(std::chrono::milliseconds timeout)
{
    return m_vdevice_input_stream->set_timeout(timeout);
}

hailo_status VDeviceInputStreamMultiplexerWrapper::flush()
{
    if (is_scheduled()) {
        auto status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__FLUSH, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    return m_vdevice_input_stream->flush();
}

Expected<std::unique_ptr<VDeviceInputStreamMultiplexerWrapper>> VDeviceInputStreamMultiplexerWrapper::create(std::shared_ptr<InputVDeviceBaseStream> vdevice_input_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
    multiplexer_ng_handle_t network_group_multiplexer_handle)
{
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<VDeviceInputStreamMultiplexerWrapper> wrapper(new (std::nothrow) VDeviceInputStreamMultiplexerWrapper(vdevice_input_stream, network_name, multiplexer,
        network_group_scheduler_handle, network_group_multiplexer_handle, status));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceInputStreamMultiplexerWrapper>> VDeviceInputStreamMultiplexerWrapper::clone(multiplexer_ng_handle_t network_group_multiplexer_handle)
{
    auto wrapper = create(m_vdevice_input_stream, m_network_name, m_multiplexer, m_network_group_scheduler_handle, network_group_multiplexer_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceInputStreamMultiplexerWrapper::VDeviceInputStreamMultiplexerWrapper(std::shared_ptr<InputVDeviceBaseStream> &vdevice_input_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
    multiplexer_ng_handle_t network_group_multiplexer_handle, hailo_status &status) :
    InputStreamBase(vdevice_input_stream->get_info(),
        vdevice_input_stream->m_nn_stream_config, vdevice_input_stream->get_network_group_activated_event()),
    m_vdevice_input_stream(vdevice_input_stream),
    m_multiplexer(multiplexer),
    m_network_group_scheduler_handle(network_group_scheduler_handle),
    m_network_group_multiplexer_handle(network_group_multiplexer_handle),
    m_network_name(network_name),
    m_is_aborted()
{
    m_is_aborted = make_unique_nothrow<std::atomic_bool>(false);
    if (nullptr == m_is_aborted) {
        status = HAILO_OUT_OF_HOST_MEMORY;
        LOGGER__ERROR("Failed to allocate memory! status = {}", status);
        return;
    }
    status = multiplexer->register_run_once_for_stream(vdevice_input_stream->name(), INPUT_RUN_ONCE_HANDLE__FLUSH, [this]
    {
        return m_vdevice_input_stream->flush();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(vdevice_input_stream->name(), INPUT_RUN_ONCE_HANDLE__ABORT, [this]
    {
        return m_vdevice_input_stream->abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(vdevice_input_stream->name(), INPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, [this]
    {
        return m_vdevice_input_stream->clear_abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }
}

const hailo_stream_info_t &VDeviceOutputStreamMultiplexerWrapper::get_info() const
{
    return m_vdevice_output_stream->get_info();
}

const CONTROL_PROTOCOL__nn_stream_config_t &VDeviceOutputStreamMultiplexerWrapper::get_nn_stream_config()
{
    return m_vdevice_output_stream->get_nn_stream_config();
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::activate_stream(uint16_t dynamic_batch_size)
{
    return m_vdevice_output_stream->activate_stream(dynamic_batch_size);
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::deactivate_stream()
{
    return m_vdevice_output_stream->deactivate_stream();
}

hailo_stream_interface_t VDeviceOutputStreamMultiplexerWrapper::get_interface() const
{
    return m_vdevice_output_stream->get_interface();
}

std::chrono::milliseconds VDeviceOutputStreamMultiplexerWrapper::get_timeout() const
{
    return m_vdevice_output_stream->get_timeout();
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::abort()
{
    if (is_scheduled()) {
        auto status = m_multiplexer->disable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        // TODO: HRT-7638
        status = m_multiplexer->run_once_for_stream(name(), OUTPUT_RUN_ONCE_HANDLE__ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_output_stream->abort();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::clear_abort()
{
    if (is_scheduled()) {
        auto status = m_multiplexer->enable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        status = m_multiplexer->run_once_for_stream(name(), OUTPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_output_stream->clear_abort();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool VDeviceOutputStreamMultiplexerWrapper::is_scheduled()
{
    return m_vdevice_output_stream->is_scheduled();
}

Expected<size_t> VDeviceOutputStreamMultiplexerWrapper::get_buffer_frames_size() const
{
    return m_vdevice_output_stream->get_buffer_frames_size();
}
Expected<size_t> VDeviceOutputStreamMultiplexerWrapper::get_pending_frames_count() const
{
    return m_vdevice_output_stream->get_pending_frames_count();
}

Expected<size_t> VDeviceOutputStreamMultiplexerWrapper::sync_read_raw_buffer(MemoryView &buffer)
{
    return m_vdevice_output_stream->sync_read_raw_buffer(buffer);
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::read_all(MemoryView &buffer)
{
    return m_vdevice_output_stream->read_all(buffer);
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::read(MemoryView buffer)
{
    uint32_t frames_to_drain_count = 0;
    if (is_scheduled()) {
        auto expected_drain_count = m_multiplexer->wait_for_read(m_network_group_multiplexer_handle, name(),
            m_vdevice_output_stream->get_timeout());
        if (HAILO_STREAM_ABORTED_BY_USER == expected_drain_count.status()) {
            return expected_drain_count.status();
        }
        CHECK_EXPECTED_AS_STATUS(expected_drain_count);

        frames_to_drain_count = expected_drain_count.release();
    }

    for (uint32_t i = 0; i < frames_to_drain_count; i++) {
        auto status = m_vdevice_output_stream->read(buffer);
        if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    auto status = m_vdevice_output_stream->read(buffer);
    if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return status;
    }
    CHECK_SUCCESS(status);

    if (is_scheduled()) {
        status = m_multiplexer->signal_read_finish(m_network_group_multiplexer_handle);
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceOutputStreamMultiplexerWrapper::set_timeout(std::chrono::milliseconds timeout)
{
    return m_vdevice_output_stream->set_timeout(timeout);
}

Expected<std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper>> VDeviceOutputStreamMultiplexerWrapper::create(std::shared_ptr<OutputVDeviceBaseStream> vdevice_output_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
    multiplexer_ng_handle_t network_group_multiplexer_handle)
{
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper> wrapper(new (std::nothrow) VDeviceOutputStreamMultiplexerWrapper(vdevice_output_stream, network_name, multiplexer,
        network_group_scheduler_handle, network_group_multiplexer_handle, status));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceOutputStreamMultiplexerWrapper>> VDeviceOutputStreamMultiplexerWrapper::clone(scheduler_ng_handle_t network_group_multiplexer_handle)
{
    auto wrapper = create(m_vdevice_output_stream, m_network_name, m_multiplexer, m_network_group_scheduler_handle, network_group_multiplexer_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceOutputStreamMultiplexerWrapper::VDeviceOutputStreamMultiplexerWrapper(std::shared_ptr<OutputVDeviceBaseStream> &vdevice_output_stream,
        std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
        multiplexer_ng_handle_t network_group_multiplexer_handle, hailo_status &status) :
    OutputStreamBase(vdevice_output_stream->get_layer_info(), vdevice_output_stream->get_info(),
        vdevice_output_stream->m_nn_stream_config, vdevice_output_stream->get_network_group_activated_event()),
    m_vdevice_output_stream(vdevice_output_stream),
    m_multiplexer(multiplexer),
    m_network_group_scheduler_handle(network_group_scheduler_handle),
    m_network_group_multiplexer_handle(network_group_multiplexer_handle),
    m_network_name(network_name)
{
    status = multiplexer->register_run_once_for_stream(vdevice_output_stream->name(), OUTPUT_RUN_ONCE_HANDLE__ABORT, [this]
    {
        return m_vdevice_output_stream->abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(vdevice_output_stream->name(), OUTPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, [this]
    {
        return m_vdevice_output_stream->clear_abort();
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }
}

} /* namespace hailort */
