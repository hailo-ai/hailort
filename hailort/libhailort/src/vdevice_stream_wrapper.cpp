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
    if (is_scheduled()) {
        auto status = m_multiplexer->disable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        // TODO: HRT-7638
        status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_input_stream->abort_impl(m_network_group_scheduler_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceInputStreamWrapper::clear_abort()
{
    if (is_scheduled()) {
        auto status = m_multiplexer->enable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_input_stream->clear_abort_impl(m_network_group_scheduler_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool VDeviceInputStreamWrapper::is_scheduled()
{
    return m_vdevice_input_stream->is_scheduled();
}

Expected<PendingBufferState> VDeviceInputStreamWrapper::send_pending_buffer()
{
    return m_vdevice_input_stream->send_pending_buffer();
}

Expected<size_t> VDeviceInputStreamWrapper::get_buffer_frames_size() const
{
    return m_vdevice_input_stream->get_buffer_frames_size();
}

Expected<size_t> VDeviceInputStreamWrapper::get_pending_frames_count() const
{
    return m_vdevice_input_stream->get_pending_frames_count();
}

hailo_status VDeviceInputStreamWrapper::reset_offset_of_pending_frames()
{
    return m_vdevice_input_stream->reset_offset_of_pending_frames();
}

Expected<size_t> VDeviceInputStreamWrapper::sync_write_raw_buffer(const MemoryView &buffer)
{
    if (is_scheduled()) {
        auto status = m_multiplexer->wait_for_write(m_network_group_multiplexer_handle);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return make_unexpected(status);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto exp = m_vdevice_input_stream->sync_write_raw_buffer_impl(buffer, m_network_group_scheduler_handle);
    if (HAILO_STREAM_INTERNAL_ABORT == exp.status()) {
        return make_unexpected(exp.status());
    }
    CHECK_EXPECTED(exp);

    if (is_scheduled()) {
        auto status = m_multiplexer->signal_write_finish();
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

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
    if (is_scheduled()) {
        auto status = m_multiplexer->run_once_for_stream(name(), INPUT_RUN_ONCE_HANDLE__FLUSH, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    return m_vdevice_input_stream->flush();
}

Expected<std::unique_ptr<VDeviceInputStreamWrapper>> VDeviceInputStreamWrapper::create(std::shared_ptr<VDeviceInputStream> vdevice_input_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
    multiplexer_ng_handle_t network_group_multiplexer_handle)
{
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<VDeviceInputStreamWrapper> wrapper(new (std::nothrow) VDeviceInputStreamWrapper(vdevice_input_stream, network_name, multiplexer,
        network_group_scheduler_handle, network_group_multiplexer_handle, status));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceInputStreamWrapper>> VDeviceInputStreamWrapper::clone(multiplexer_ng_handle_t network_group_multiplexer_handle)
{
    auto wrapper = create(m_vdevice_input_stream, m_network_name, m_multiplexer, m_network_group_scheduler_handle, network_group_multiplexer_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceInputStreamWrapper::VDeviceInputStreamWrapper(std::shared_ptr<VDeviceInputStream> &vdevice_input_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
    multiplexer_ng_handle_t network_group_multiplexer_handle, hailo_status &status) :
    InputStreamBase(vdevice_input_stream->get_info(),
        vdevice_input_stream->m_nn_stream_config, vdevice_input_stream->get_network_group_activated_event()),
    m_vdevice_input_stream(vdevice_input_stream),
    m_multiplexer(multiplexer),
    m_network_group_scheduler_handle(network_group_scheduler_handle),
    m_network_group_multiplexer_handle(network_group_multiplexer_handle),
    m_network_name(network_name)
{
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
        return m_vdevice_input_stream->abort_impl(m_network_group_scheduler_handle);
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(vdevice_input_stream->name(), INPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, [this]
    {
        return m_vdevice_input_stream->clear_abort_impl(m_network_group_scheduler_handle);
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }
}

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
    if (is_scheduled()) {
        auto status = m_multiplexer->disable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        // TODO: HRT-7638
        status = m_multiplexer->run_once_for_stream(name(), OUTPUT_RUN_ONCE_HANDLE__ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_output_stream->abort_impl(m_network_group_scheduler_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status VDeviceOutputStreamWrapper::clear_abort()
{
    if (is_scheduled()) {
        auto status = m_multiplexer->enable_network_group(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        status = m_multiplexer->run_once_for_stream(name(), OUTPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = m_vdevice_output_stream->clear_abort_impl(m_network_group_scheduler_handle);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool VDeviceOutputStreamWrapper::is_scheduled()
{
    return m_vdevice_output_stream->is_scheduled();
}

Expected<size_t> VDeviceOutputStreamWrapper::get_buffer_frames_size() const
{
    return m_vdevice_output_stream->get_buffer_frames_size();
}
Expected<size_t> VDeviceOutputStreamWrapper::get_pending_frames_count() const
{
    return m_vdevice_output_stream->get_pending_frames_count();
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
    if (is_scheduled()) {
        auto status = m_multiplexer->wait_for_read(m_network_group_multiplexer_handle, name(),
            m_vdevice_output_stream->get_timeout());
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    auto status = m_vdevice_output_stream->read_impl(buffer, m_network_group_scheduler_handle);
    if ((HAILO_STREAM_INTERNAL_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
        return status;
    }
    CHECK_SUCCESS(status);

    if (is_scheduled()) {
        status = m_multiplexer->signal_read_finish(m_network_group_multiplexer_handle);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status VDeviceOutputStreamWrapper::set_timeout(std::chrono::milliseconds timeout)
{
    return m_vdevice_output_stream->set_timeout(timeout);
}

Expected<std::unique_ptr<VDeviceOutputStreamWrapper>> VDeviceOutputStreamWrapper::create(std::shared_ptr<VDeviceOutputStream> vdevice_output_stream,
    std::string network_name, std::shared_ptr<PipelineMultiplexer> multiplexer, scheduler_ng_handle_t network_group_scheduler_handle,
    multiplexer_ng_handle_t network_group_multiplexer_handle)
{
    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<VDeviceOutputStreamWrapper> wrapper(new (std::nothrow) VDeviceOutputStreamWrapper(vdevice_output_stream, network_name, multiplexer,
        network_group_scheduler_handle, network_group_multiplexer_handle, status));
    CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return wrapper;
}

Expected<std::unique_ptr<VDeviceOutputStreamWrapper>> VDeviceOutputStreamWrapper::clone(scheduler_ng_handle_t network_group_multiplexer_handle)
{
    auto wrapper = create(m_vdevice_output_stream, m_network_name, m_multiplexer, m_network_group_scheduler_handle, network_group_multiplexer_handle);
    CHECK_EXPECTED(wrapper);

    return wrapper;
}

VDeviceOutputStreamWrapper::VDeviceOutputStreamWrapper(std::shared_ptr<VDeviceOutputStream> &vdevice_output_stream,
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
        return m_vdevice_output_stream->abort_impl(m_network_group_scheduler_handle);
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }

    status = multiplexer->register_run_once_for_stream(vdevice_output_stream->name(), OUTPUT_RUN_ONCE_HANDLE__CLEAR_ABORT, [this]
    {
        return m_vdevice_output_stream->clear_abort_impl(m_network_group_scheduler_handle);
    });
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("register_run_once_for_stream failed! status = {}", status);
        return;
    }
}

} /* namespace hailort */
