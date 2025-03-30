/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream.cpp
 * @brief Implementation of the virtual stream
 **/

#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "hailo/stream.hpp"
#include "hailo/vstream.hpp"
#include "hailo/hef.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/hailort_defaults.hpp"
#include "hailo/hailort_common.hpp"
#include "net_flow/pipeline/pipeline_internal.hpp"
#include "stream_common/stream_internal.hpp"

#include "net_flow/pipeline/vstream_internal.hpp"
#include <cstdint>
#include <math.h>
#include <memory>

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "rpc/rpc_definitions.hpp"
#include "service/rpc_client_utils.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS

#include <unordered_set>


namespace hailort
{

static std::map<std::string, AccumulatorPtr> get_pipeline_accumulators_by_type(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline, AccumulatorType accumulator_type);

static std::map<std::string, std::vector<AccumulatorPtr>> get_pipeline_queue_size_accumulators(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline);

BaseVStream::BaseVStream(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
                         std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                         AccumulatorPtr pipeline_latency_accumulator, EventPtr &&core_op_activated_event,
                         hailo_status &output_status) :
    m_vstream_info(vstream_info),
    m_quant_infos(quant_infos),
    m_vstream_params(vstream_params),
    m_measure_pipeline_latency((vstream_params.vstream_stats_flags & HAILO_VSTREAM_STATS_MEASURE_LATENCY) != 0),
    m_entry_element(pipeline_entry),
    m_pipeline(std::move(pipeline)),
    m_is_activated(false),
    m_is_aborted(false),
    m_pipeline_status(std::move(pipeline_status)),
    m_core_op_activated_event(std::move(core_op_activated_event)),
    m_fps_accumulators(get_pipeline_accumulators_by_type(m_pipeline, AccumulatorType::FPS)),
    m_latency_accumulators(get_pipeline_accumulators_by_type(m_pipeline, AccumulatorType::LATENCY)),
    m_queue_size_accumulators(get_pipeline_queue_size_accumulators(m_pipeline)),
    m_pipeline_latency_accumulator(pipeline_latency_accumulator)
{
    output_status = start_vstream();
}

BaseVStream::BaseVStream(BaseVStream &&other) noexcept :
    m_vstream_info(std::move(other.m_vstream_info)),
    m_vstream_params(std::move(other.m_vstream_params)),
    m_measure_pipeline_latency(std::move(other.m_measure_pipeline_latency)),
    m_entry_element(std::move(other.m_entry_element)),
    m_pipeline(std::move(other.m_pipeline)),
    m_is_activated(std::exchange(other.m_is_activated, false)),
    m_is_aborted(std::exchange(other.m_is_aborted, false)),
    m_pipeline_status(std::move(other.m_pipeline_status)),
    m_core_op_activated_event(std::move(other.m_core_op_activated_event)),
    m_fps_accumulators(std::move(other.m_fps_accumulators)),
    m_latency_accumulators(std::move(other.m_latency_accumulators)),
    m_queue_size_accumulators(std::move(other.m_queue_size_accumulators)),
    m_pipeline_latency_accumulator(std::move(other.m_pipeline_latency_accumulator))
{}

BaseVStream& BaseVStream::operator=(BaseVStream &&other) noexcept
{
    if (this != &other) {
        // operator= is used only for vstream creation BEFORE activation. otherwise we should deactivate vstream here
        assert(!m_is_activated);
        m_vstream_info = std::move(other.m_vstream_info);
        m_quant_infos = std::move(other.m_quant_infos);
        m_vstream_params = std::move(other.m_vstream_params);
        m_measure_pipeline_latency = std::move(other.m_measure_pipeline_latency);
        m_entry_element = std::move(other.m_entry_element);
        m_pipeline = std::move(other.m_pipeline);
        m_is_activated = std::exchange(other.m_is_activated, false);
        m_is_aborted = std::exchange(other.m_is_aborted, false);
        m_pipeline_status = std::move(other.m_pipeline_status);
        m_core_op_activated_event = std::move(other.m_core_op_activated_event);
        m_fps_accumulators = std::move(other.m_fps_accumulators);
        m_latency_accumulators = std::move(other.m_latency_accumulators);
        m_queue_size_accumulators = std::move(other.m_queue_size_accumulators);
        m_pipeline_latency_accumulator = std::move(other.m_pipeline_latency_accumulator);
    }
    return *this;
}

hailo_status BaseVStream::start_vstream()
{
    auto status = resume();
    CHECK(((status == HAILO_SUCCESS) || (status == HAILO_STREAM_NOT_ACTIVATED)), status,
        "Failed to resume stream in {}", name());

    LOGGER__DEBUG("Activating {}...", name());
    status = m_entry_element->activate();
    CHECK_SUCCESS(status);

    m_is_activated = true;
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::abort()
{
    auto status = m_entry_element->abort();
    CHECK_SUCCESS(status);
    m_is_aborted = true;

    return HAILO_SUCCESS;
}

hailo_status BaseVStream::resume()
{
    auto status = m_entry_element->clear_abort();
    CHECK_SUCCESS(status);
    m_is_aborted = false;

    if (m_is_activated) {
        status = m_entry_element->activate();
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::stop_vstream()
{
    hailo_status status = HAILO_SUCCESS;
    if (m_is_activated) {
        m_is_activated = false;
        status = m_entry_element->deactivate();
        if (HAILO_SUCCESS != status) {
            LOGGER__WARNING("Failed deactivate of vstream {} status {}", name(), status);
        }

        // If VStream was aborted, do not clear low-level stream abortion,
        // otherwise flush would be called on low-level stream d-tor when there is no receiver.
        auto should_clear_abort = (!m_is_aborted);
        status = m_entry_element->post_deactivate(should_clear_abort);
        if (HAILO_SUCCESS != status) {
            LOGGER__WARNING("Failed post deactivate of vstream {} status {}", name(), status);
        }
    }
    return status;
}

hailo_status BaseVStream::stop_and_clear()
{
    auto status = stop_vstream();
    CHECK_SUCCESS(status);

    status = m_entry_element->clear();
    CHECK_SUCCESS(status, "Failed clearing vstream {}", name());

    const auto curr_pipeline_status = m_pipeline_status->load();
    if (HAILO_SUCCESS != curr_pipeline_status) {
        LOGGER__TRACE("Overwritting current pipeline status {}", curr_pipeline_status);
        m_pipeline_status->store(HAILO_SUCCESS);
    }

    return status;
}

hailo_status BaseVStream::before_fork()
{
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::after_fork_in_parent()
{
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::after_fork_in_child()
{
    return HAILO_SUCCESS;
}

size_t BaseVStream::get_frame_size() const
{
    return HailoRTCommon::get_frame_size(m_vstream_info, m_vstream_params.user_buffer_format);
}

const hailo_vstream_info_t &BaseVStream::get_info() const
{
    return m_vstream_info;
}

const std::vector<hailo_quant_info_t> &BaseVStream::get_quant_infos() const
{
    return m_quant_infos;
}

const hailo_format_t &BaseVStream::get_user_buffer_format() const
{
    return m_vstream_params.user_buffer_format;
}

std::string BaseVStream::name() const
{
    return std::string(m_vstream_info.name);
}

std::string BaseVStream::network_name() const
{
    return std::string(m_vstream_info.network_name);
}

const std::map<std::string, AccumulatorPtr> &BaseVStream::get_fps_accumulators() const
{
    return m_fps_accumulators;
}

const std::map<std::string, AccumulatorPtr> &BaseVStream::get_latency_accumulators() const
{
    return m_latency_accumulators;
}

const std::map<std::string, std::vector<AccumulatorPtr>> &BaseVStream::get_queue_size_accumulators() const
{
    return m_queue_size_accumulators;
}

AccumulatorPtr BaseVStream::get_pipeline_latency_accumulator() const
{
    return m_pipeline_latency_accumulator;
}


const std::vector<std::shared_ptr<PipelineElement>> &BaseVStream::get_pipeline() const
{
    return m_pipeline;
}

Expected<InputVStream> InputVStream::create(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr core_op_activated_event,
        AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream_internal = InputVStreamInternal::create(vstream_info, quant_infos, vstream_params, pipeline_entry, pipeline_exit,
        std::move(pipeline), std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream_internal);

    InputVStream vstream(vstream_internal.release());
    return vstream;
}

hailo_status InputVStream::write(const MemoryView &buffer)
{
    return m_vstream->write(std::move(buffer));
}

hailo_status InputVStream::write(const hailo_pix_buffer_t &buffer)
{
    CHECK(HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == buffer.memory_type, HAILO_NOT_SUPPORTED, "Memory type of pix buffer must be of type USERPTR!");

    // If only one plane is passed, address it as memview
    if (1 == buffer.number_of_planes) {
        return write(MemoryView(buffer.planes[0].user_ptr, buffer.planes[0].bytes_used));
    }

    // If model is multi planar, pass the pix buffer
    if (m_vstream->is_multi_planar()){
        return m_vstream->write(buffer);
    }

    // Other cases - allocate a contiguous buffer to hold all plains
    bool is_contiguous = true;
    uint32_t planes_total_size = 0;
    /* assuming contiguous memory. If not, this will be overriden by the coming loop */
    void *data_ptr = buffer.planes[0].user_ptr;

    /* calculate total data size by summing the planes' sizes and check if the planes are contiguous */
    for (uint32_t plane_index = 0; plane_index < buffer.number_of_planes; plane_index++){
        auto &plane = buffer.planes[plane_index];
        planes_total_size += plane.bytes_used;

        if (is_contiguous && (plane_index + 1 < buffer.number_of_planes)){
            auto &next_plane = buffer.planes[plane_index+1];
            if ((static_cast<uint8_t*>(plane.user_ptr) + plane.bytes_used) != next_plane.user_ptr){
                is_contiguous = false;
            }
        }
    }

    BufferPtr contiguous_buffer = nullptr;
    if (! is_contiguous) {
        /* copy to a contiguous buffer, and then pass it */
        auto expected_buffer = Buffer::create_shared(planes_total_size);
        CHECK_EXPECTED_AS_STATUS(expected_buffer);
        contiguous_buffer = expected_buffer.release();
        uint32_t copied_bytes = 0;

        for (uint32_t plane_index = 0; plane_index < buffer.number_of_planes; plane_index++){
            auto &plane = buffer.planes[plane_index];
            std::memcpy(contiguous_buffer->data() + copied_bytes, plane.user_ptr, plane.bytes_used);
            copied_bytes += plane.bytes_used;
        }

        data_ptr = contiguous_buffer->data();
    }

    return m_vstream->write(std::move(MemoryView(data_ptr, planes_total_size)));
}

hailo_status InputVStream::flush()
{
    return m_vstream->flush();
}

hailo_status InputVStream::clear(std::vector<InputVStream> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status InputVStream::clear(std::vector<std::reference_wrapper<InputVStream>> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.get().stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.get().start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status InputVStream::abort()
{
    return m_vstream->abort();
}

hailo_status InputVStream::resume()
{
    return m_vstream->resume();
}

size_t InputVStream::get_frame_size() const
{
    return m_vstream->get_frame_size();
}

const hailo_vstream_info_t &InputVStream::get_info() const
{
    return m_vstream->get_info();
}

const std::vector<hailo_quant_info_t> &InputVStream::get_quant_infos() const
{
    return m_vstream->get_quant_infos();
}

const hailo_format_t &InputVStream::get_user_buffer_format() const
{
    return m_vstream->get_user_buffer_format();
}

std::string InputVStream::name() const
{
    return m_vstream->name();
}

std::string InputVStream::network_name() const
{
    return m_vstream->network_name();
}

const std::map<std::string, AccumulatorPtr> &InputVStream::get_fps_accumulators() const
{
    return m_vstream->get_fps_accumulators();
}

const std::map<std::string, AccumulatorPtr> &InputVStream::get_latency_accumulators() const
{
    return m_vstream->get_latency_accumulators();
}

const std::map<std::string, std::vector<AccumulatorPtr>> &InputVStream::get_queue_size_accumulators() const
{
    return m_vstream->get_queue_size_accumulators();
}

AccumulatorPtr InputVStream::get_pipeline_latency_accumulator() const
{
    return m_vstream->get_pipeline_latency_accumulator();
}

const std::vector<std::shared_ptr<PipelineElement>> &InputVStream::get_pipeline() const
{
    return m_vstream->get_pipeline();
}

hailo_status InputVStream::start_vstream()
{
    return m_vstream->start_vstream();
}

hailo_status InputVStream::stop_vstream()
{
    return m_vstream->stop_vstream();
}

hailo_status InputVStream::stop_and_clear()
{
    return m_vstream->stop_and_clear();
}

std::string InputVStream::get_pipeline_description() const
{
    return m_vstream->get_pipeline_description();
}

bool InputVStream::is_aborted()
{
    return m_vstream->is_aborted();
}

bool InputVStream::is_multi_planar()
{
    return m_vstream->is_multi_planar();
}


hailo_status InputVStream::before_fork()
{
    return m_vstream->before_fork();
}

hailo_status InputVStream::after_fork_in_parent()
{
    return m_vstream->after_fork_in_parent();
}

hailo_status InputVStream::after_fork_in_child()
{
    return m_vstream->after_fork_in_child();
}

InputVStream::InputVStream(std::shared_ptr<InputVStreamInternal> vstream) : m_vstream(std::move(vstream)) {}

Expected<OutputVStream> OutputVStream::create(
        const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream_internal = OutputVStreamInternal::create(vstream_info, quant_infos, vstream_params, pipeline_entry,
        std::move(pipeline), std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream_internal);

    OutputVStream vstream(vstream_internal.release());
    return vstream;
}

hailo_status OutputVStream::read(MemoryView buffer)
{
    auto status = m_vstream->read(std::move(buffer));
    if (HAILO_TIMEOUT == status) {
        auto clear_status = m_vstream->clear();
        if (HAILO_SUCCESS != clear_status) {
            LOGGER__ERROR("Failed to clear output pipeline '{}' after a timeout. This pipeline is not usable and should be re-created.", name());
        }
    }
    return status;
}

hailo_status OutputVStream::clear(std::vector<OutputVStream> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status OutputVStream::abort()
{
    return m_vstream->abort();
}

hailo_status OutputVStream::resume()
{
    return m_vstream->resume();
}

hailo_status OutputVStream::clear(std::vector<std::reference_wrapper<OutputVStream>> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.get().stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.get().start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

size_t OutputVStream::get_frame_size() const
{
    return m_vstream->get_frame_size();
}

const hailo_vstream_info_t &OutputVStream::get_info() const
{
    return m_vstream->get_info();
}

const std::vector<hailo_quant_info_t> &OutputVStream::get_quant_infos() const
{
    return m_vstream->get_quant_infos();
}

const hailo_format_t &OutputVStream::get_user_buffer_format() const
{
    return m_vstream->get_user_buffer_format();
}

std::string OutputVStream::name() const
{
    return m_vstream->name();
}

std::string OutputVStream::network_name() const
{
    return m_vstream->network_name();
}

const std::map<std::string, AccumulatorPtr> &OutputVStream::get_fps_accumulators() const
{
    return m_vstream->get_fps_accumulators();
}

const std::map<std::string, AccumulatorPtr> &OutputVStream::get_latency_accumulators() const
{
    return m_vstream->get_latency_accumulators();
}

const std::map<std::string, std::vector<AccumulatorPtr>> &OutputVStream::get_queue_size_accumulators() const
{
    return m_vstream->get_queue_size_accumulators();
}

AccumulatorPtr OutputVStream::get_pipeline_latency_accumulator() const
{
    return m_vstream->get_pipeline_latency_accumulator();
}

const std::vector<std::shared_ptr<PipelineElement>> &OutputVStream::get_pipeline() const
{
    return m_vstream->get_pipeline();
}

hailo_status OutputVStream::start_vstream()
{
    return m_vstream->start_vstream();
}

hailo_status OutputVStream::stop_vstream()
{
    return m_vstream->stop_vstream();
}

hailo_status OutputVStream::stop_and_clear()
{
    return m_vstream->stop_and_clear();
}

std::string OutputVStream::get_pipeline_description() const
{
    return m_vstream->get_pipeline_description();
}

bool OutputVStream::is_aborted()
{
    return m_vstream->is_aborted();
}

hailo_status OutputVStream::before_fork()
{
    return m_vstream->before_fork();
}

hailo_status OutputVStream::after_fork_in_parent()
{
    return m_vstream->after_fork_in_parent();
}

hailo_status OutputVStream::after_fork_in_child()
{
    return m_vstream->after_fork_in_child();
}

hailo_status OutputVStream::set_nms_score_threshold(float32_t threshold)
{
    return m_vstream->set_nms_score_threshold(threshold);
}

hailo_status OutputVStream::set_nms_iou_threshold(float32_t threshold)
{
    return m_vstream->set_nms_iou_threshold(threshold);
}

hailo_status OutputVStream::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    return m_vstream->set_nms_max_proposals_per_class(max_proposals_per_class);
}

hailo_status OutputVStream::set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
{
    return m_vstream->set_nms_max_accumulated_mask_size(max_accumulated_mask_size);
}

OutputVStream::OutputVStream(std::shared_ptr<OutputVStreamInternal> vstream) : m_vstream(std::move(vstream)) {}

std::map<std::string, AccumulatorPtr> get_pipeline_accumulators_by_type(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline, AccumulatorType accumulator_type)
{
    std::map<std::string, AccumulatorPtr> result;
    for (const auto &elem : pipeline) {
        if (nullptr == elem) {
            continue;
        }

        AccumulatorPtr accumulator = nullptr;
        if (AccumulatorType::FPS == accumulator_type) {
            accumulator = elem->get_fps_accumulator();
        } else if (AccumulatorType::LATENCY == accumulator_type) {
            accumulator = elem->get_latency_accumulator();
        } else {
            continue;
        }

        if (nullptr != accumulator) {
            result.emplace(elem->name(), accumulator);
        }
    }

    return result;
}

std::map<std::string, std::vector<AccumulatorPtr>> get_pipeline_queue_size_accumulators(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline)
{
    std::map<std::string, std::vector<AccumulatorPtr>> result;
    for (const auto &elem : pipeline) {
        if (nullptr == elem) {
            continue;
        }

        const auto accumulators = elem->get_queue_size_accumulators();
        if (0 != accumulators.size()) {
            result.emplace(elem->name(), accumulators);
        }
    }

    return result;
}

Expected<std::shared_ptr<InputVStreamInternal>> InputVStreamInternal::create(const hailo_vstream_info_t &vstream_info,
    const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
    std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr core_op_activated_event,
    AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream = InputVStreamImpl::create(vstream_info, quant_infos, vstream_params, pipeline_entry, pipeline_exit,
        std::move(pipeline), std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream);
    auto vstream_ptr = std::shared_ptr<InputVStreamInternal>(vstream.release());
    return vstream_ptr;
}

InputVStreamInternal::InputVStreamInternal(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
    const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator, EventPtr &&core_op_activated_event,
    hailo_status &output_status) :
    BaseVStream(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
        pipeline_latency_accumulator, std::move(core_op_activated_event), output_status){}

Expected<std::shared_ptr<InputVStreamImpl>> InputVStreamImpl::create(const hailo_vstream_info_t &vstream_info,
    const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
    std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr core_op_activated_event,
    AccumulatorPtr pipeline_latency_accumulator)
{
    hailo_status status = HAILO_UNINITIALIZED;

    if (nullptr != pipeline_latency_accumulator) {
        if (pipeline_exit) {
            pipeline_exit->sink().set_push_complete_callback([pipeline_latency_accumulator](const PipelineBuffer::Metadata& metadata) {
                    const auto duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(
                        std::chrono::steady_clock::now() - metadata.get_start_time()).count();
                    pipeline_latency_accumulator->add_data_point(duration_sec);
                });
        }
    }

    auto vstream_ptr = std::shared_ptr<InputVStreamImpl>(new InputVStreamImpl(vstream_info, quant_infos, vstream_params, std::move(pipeline_entry), std::move(pipeline),
        std::move(pipeline_status), pipeline_latency_accumulator, std::move(core_op_activated_event), status));
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to create virtual stream");

    return vstream_ptr;
}

InputVStreamImpl::InputVStreamImpl(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
    std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator,
    EventPtr core_op_activated_event, hailo_status &output_status) :
    InputVStreamInternal(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
        pipeline_latency_accumulator, std::move(core_op_activated_event), output_status)
{
    // TODO: propagate a flag instead of using dynamic_pointer_cast (will be disabled when we'll disable RTTI)
    m_is_multi_planar = (nullptr != std::dynamic_pointer_cast<PixBufferElement>(pipeline_entry));

    if (HAILO_SUCCESS != output_status) {
        return;
    }

    LOGGER__INFO("Creating {}...", name());
}

InputVStreamImpl::~InputVStreamImpl()
{
    (void)stop_vstream();
}

hailo_status InputVStreamImpl::write(const MemoryView &buffer)
{
    if (nullptr != m_core_op_activated_event) {
        CHECK(m_is_activated, HAILO_VSTREAM_PIPELINE_NOT_ACTIVATED, "Failed to write buffer! Virtual stream {} is not activated!", name());
        auto status = m_core_op_activated_event->wait(std::chrono::milliseconds(0));
        CHECK(HAILO_TIMEOUT != status, HAILO_NETWORK_GROUP_NOT_ACTIVATED,
            "Trying to write to vstream {} before its network group is activated", name());
    }

    assert(1 == m_entry_element->sinks().size());
    auto status = m_entry_element->sinks()[0].run_push(PipelineBuffer(buffer, [](hailo_status){}, HAILO_SUCCESS, false, BufferPoolWeakPtr(), m_measure_pipeline_latency));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Sending to VStream was shutdown!");
        status = m_pipeline_status->load();
    }
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Sending to VStream was aborted!");
        return HAILO_STREAM_ABORT;
    }
    return status;
}

hailo_status InputVStreamImpl::write(const hailo_pix_buffer_t &buffer)
{
    CHECK(HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == buffer.memory_type, HAILO_NOT_SUPPORTED, "Memory type of pix buffer must be of type USERPTR!");

    if (nullptr != m_core_op_activated_event) {
        CHECK(m_is_activated, HAILO_VSTREAM_PIPELINE_NOT_ACTIVATED, "Failed to write buffer! Virtual stream {} is not activated!", name());
        auto status = m_core_op_activated_event->wait(std::chrono::milliseconds(0));
        CHECK(HAILO_TIMEOUT != status, HAILO_NETWORK_GROUP_NOT_ACTIVATED,
            "Trying to write to vstream {} before its network group is activated", name());
    }

    assert(1 == m_entry_element->sinks().size());
    auto status = m_entry_element->sinks()[0].run_push(PipelineBuffer(buffer));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Sending to VStream was shutdown!");
        status = m_pipeline_status->load();
    }
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Sending to VStream was aborted!");
        return HAILO_STREAM_ABORT;
    }
    return status;
}

hailo_status InputVStreamImpl::flush()
{
    assert(1 == m_entry_element->sinks().size());
    auto status =  m_entry_element->sinks()[0].run_push(PipelineBuffer(PipelineBuffer::Type::FLUSH));
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Sending to VStream was aborted!");
        return HAILO_STREAM_ABORT;
    }
    CHECK_SUCCESS(status);

    status = m_entry_element->flush();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool InputVStreamImpl::is_multi_planar() const
{
    return m_is_multi_planar;
}

#ifdef HAILO_SUPPORT_MULTI_PROCESS
Expected<std::shared_ptr<InputVStreamClient>> InputVStreamClient::create(VStreamIdentifier &&identifier, const std::chrono::milliseconds &timeout)
{
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(-1);
    auto channel = grpc::CreateCustomChannel(hailort::HAILORT_SERVICE_ADDRESS, grpc::InsecureChannelCredentials(), ch_args);
    CHECK_AS_EXPECTED(channel != nullptr, HAILO_INTERNAL_FAILURE);

    auto client = make_unique_nothrow<HailoRtRpcClient>(channel);
    CHECK_AS_EXPECTED(client != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto user_buffer_format = client->InputVStream_get_user_buffer_format(identifier);
    CHECK_EXPECTED(user_buffer_format);

    auto vstream_info = client->InputVStream_get_info(identifier);
    CHECK_EXPECTED(vstream_info);

    return std::shared_ptr<InputVStreamClient>(new InputVStreamClient(std::move(client), std::move(identifier),
        user_buffer_format.release(), vstream_info.release(), timeout));
}

InputVStreamClient::InputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, VStreamIdentifier &&identifier, hailo_format_t &&user_buffer_format,
    hailo_vstream_info_t &&info, const std::chrono::milliseconds &timeout) :
        m_client(std::move(client)), m_identifier(std::move(identifier)), m_user_buffer_format(user_buffer_format), m_info(info), m_timeout(timeout) {}

InputVStreamClient::~InputVStreamClient()
{
    auto reply = m_client->InputVStream_release(m_identifier, OsUtils::get_curr_pid());
    if (reply != HAILO_SUCCESS) {
        LOGGER__CRITICAL("InputVStream_release failed!");
    }
}

hailo_status InputVStreamClient::write(const MemoryView &buffer)
{
    return m_client->InputVStream_write(m_identifier, buffer, m_timeout);
}

hailo_status InputVStreamClient::write(const hailo_pix_buffer_t &buffer)
{
    return m_client->InputVStream_write(m_identifier, buffer, m_timeout);
}

hailo_status InputVStreamClient::flush()
{
    return m_client->InputVStream_flush(m_identifier);
}

bool InputVStreamClient::is_multi_planar() const
{
    auto is_multi_planar_exp = m_client->InputVStream_is_multi_planar(m_identifier);
    if (!is_multi_planar_exp) {
        LOGGER__CRITICAL("InputVStream_is_multi_planar failed with status={}", is_multi_planar_exp.status());
        return true;
    }
    return is_multi_planar_exp.release();
}

hailo_status InputVStreamClient::abort()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto abort_client = expected_client.release();
    return abort_client->InputVStream_abort(m_identifier);
}

hailo_status InputVStreamClient::resume()
{
    return m_client->InputVStream_resume(m_identifier);
}

hailo_status InputVStreamClient::stop_and_clear()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto stop_and_clear_client = expected_client.release();

    return stop_and_clear_client->InputVStream_stop_and_clear(m_identifier);
}

hailo_status InputVStreamClient::start_vstream()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto start_vstream_client = expected_client.release();

    return start_vstream_client->InputVStream_start_vstream(m_identifier);
}

size_t InputVStreamClient::get_frame_size() const
{
    auto frame_size = m_client->InputVStream_get_frame_size(m_identifier);
    if (!frame_size) {
        LOGGER__CRITICAL("InputVStream_get_frame_size failed with status={}", frame_size.status());
        return 0;
    }
    return frame_size.release();
}

const hailo_vstream_info_t &InputVStreamClient::get_info() const
{
    return m_info;
}

const hailo_format_t &InputVStreamClient::get_user_buffer_format() const
{
    return m_user_buffer_format;
}

std::string InputVStreamClient::name() const
{
    auto expected_name = m_client->InputVStream_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("InputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

std::string InputVStreamClient::network_name() const
{
    auto expected_name = m_client->InputVStream_network_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("InputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

const std::map<std::string, AccumulatorPtr> &InputVStreamClient::get_fps_accumulators() const
{
    LOGGER__ERROR("InputVStream::get_fps_accumulators function is not supported when using multi-process service");
    return m_fps_accumulators;
}
const std::map<std::string, AccumulatorPtr> &InputVStreamClient::get_latency_accumulators() const
{
    LOGGER__ERROR("InputVStream::get_latency_accumulators function is not supported when using multi-process service");
    return m_latency_accumulators;
}

const std::map<std::string, std::vector<AccumulatorPtr>> &InputVStreamClient::get_queue_size_accumulators() const
{
    LOGGER__ERROR("InputVStream::get_queue_size_accumulators function is not supported when using multi-process service");
    return m_queue_size_accumulators;
}
AccumulatorPtr InputVStreamClient::get_pipeline_latency_accumulator() const
{
    LOGGER__ERROR("InputVStream::get_pipeline_latency_accumulator function is not supported when using multi-process service");
    return m_pipeline_latency_accumulator;
}
const std::vector<std::shared_ptr<PipelineElement>> &InputVStreamClient::get_pipeline() const
{
    LOGGER__ERROR("InputVStream::get_pipeline function is not supported when using multi-process service");
    return m_pipeline;
}

hailo_status InputVStreamClient::create_client()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    m_client = expected_client.release();
    return HAILO_SUCCESS;
}

hailo_status InputVStreamClient::before_fork()
{
    m_client.reset();
    return HAILO_SUCCESS;
}

hailo_status InputVStreamClient::after_fork_in_parent()
{
    return create_client();
}

hailo_status InputVStreamClient::after_fork_in_child()
{
    return create_client();
}

bool InputVStreamClient::is_aborted()
{
    auto is_aborted_exp = m_client->InputVStream_is_aborted(m_identifier);
    if (!is_aborted_exp) {
        LOGGER__CRITICAL("InputVStream_is_aborted failed with status={}", is_aborted_exp.status());
        return true;
    }
    return is_aborted_exp.release();
}

#endif // HAILO_SUPPORT_MULTI_PROCESS

std::string InputVStreamInternal::get_pipeline_description() const
{
    std::stringstream pipeline_str;
    pipeline_str << "Input pipeline '" << name() << "': ";
    for (const auto &element : m_pipeline) {
        pipeline_str << element->description() << " >> ";
    }
    pipeline_str << "HW";
    return pipeline_str.str();
}

Expected<std::shared_ptr<OutputVStreamInternal>> OutputVStreamInternal::create(const hailo_vstream_info_t &vstream_info,
    const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
    std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream = OutputVStreamImpl::create(vstream_info, quant_infos, vstream_params, pipeline_entry,
        std::move(pipeline), std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream);
    auto vstream_ptr = std::shared_ptr<OutputVStreamInternal>(vstream.release());
    return vstream_ptr;
}

OutputVStreamInternal::OutputVStreamInternal(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
    const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
    std::vector<std::shared_ptr<PipelineElement>> &&pipeline, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    AccumulatorPtr pipeline_latency_accumulator, EventPtr core_op_activated_event, hailo_status &output_status) :
    BaseVStream(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
        pipeline_latency_accumulator, std::move(core_op_activated_event), output_status)
{
    // Reversing the order of pipeline-elements, for the destruction flow to work in the right order (from user-side to hw-side)
    std::reverse(m_pipeline.begin(), m_pipeline.end());
}

hailo_status OutputVStreamInternal::clear()
{
    CHECK_SUCCESS(stop_and_clear());
    CHECK_SUCCESS(start_vstream());
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<OutputVStreamImpl>> OutputVStreamImpl::create(const hailo_vstream_info_t &vstream_info,
    const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
    std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator)
{
    hailo_status status = HAILO_UNINITIALIZED;

    CHECK_AS_EXPECTED(1 == pipeline_entry->sources().size(), HAILO_INVALID_ARGUMENT,
        "OutputVStream's entry element is expected to have one source");

    if (nullptr != pipeline_latency_accumulator) {
        pipeline_entry->sources()[0].set_pull_complete_callback([pipeline_latency_accumulator](const PipelineBuffer::Metadata& metadata) {
                const auto duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(
                    std::chrono::steady_clock::now() - metadata.get_start_time()).count();
                pipeline_latency_accumulator->add_data_point(duration_sec);
            });
    }

    auto vstream_ptr = std::shared_ptr<OutputVStreamImpl>(new OutputVStreamImpl(vstream_info, quant_infos, vstream_params, std::move(pipeline_entry),
        std::move(pipeline), std::move(pipeline_status), pipeline_latency_accumulator, std::move(core_op_activated_event), status));
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to create virtual stream");

    return vstream_ptr;
}

std::string OutputVStreamInternal::get_pipeline_description() const
{
    // We save elements in a reverse order for destruction order, so we reverse again befor printing.
    std::vector<std::shared_ptr<PipelineElement>> reversed_pipeline;
    std::reverse_copy(m_pipeline.begin(), m_pipeline.end(), std::back_inserter(reversed_pipeline));

    std::stringstream pipeline_str;
    pipeline_str << "Output pipeline '" << name() << "': HW";
    for (const auto &element : reversed_pipeline) {
        pipeline_str << " >> " << element->description();
    }
    return pipeline_str.str();
}

OutputVStreamImpl::OutputVStreamImpl(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
    const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator,
    EventPtr core_op_activated_event, hailo_status &output_status) :
    OutputVStreamInternal(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
        pipeline_latency_accumulator, std::move(core_op_activated_event), output_status)
{
    if (HAILO_SUCCESS != output_status) {
        return;
    }

    LOGGER__INFO("Creating {}...", name());
}

OutputVStreamImpl::~OutputVStreamImpl()
{
    (void)stop_vstream();
}

hailo_status OutputVStreamImpl::read(MemoryView buffer)
{
    if (nullptr != m_core_op_activated_event) {
        CHECK(m_is_activated, HAILO_VSTREAM_PIPELINE_NOT_ACTIVATED, "read() failed! Virtual stream {} is not activated!", name());
        auto status = m_core_op_activated_event->wait(std::chrono::milliseconds(0));
        if (HAILO_TIMEOUT == status) {
            LOGGER__INFO("Trying to read from vstream {} before its network_group is activated", name());
            return HAILO_NETWORK_GROUP_NOT_ACTIVATED;
        }
        CHECK_SUCCESS(status);
    }

    assert(1 == m_entry_element->sources().size());
    auto recv_buffer = m_entry_element->sources()[0].run_pull(PipelineBuffer(buffer, [](hailo_status){},  HAILO_SUCCESS, false, BufferPoolWeakPtr(), m_measure_pipeline_latency));
    auto status = recv_buffer.status();
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Receiving to VStream was shutdown!");
        status = m_pipeline_status->load();
    }

    return status;
}

hailo_status OutputVStreamImpl::set_nms_score_threshold(float32_t threshold)
{
    auto status = HAILO_INVALID_OPERATION; // Assuming there is no valid element
    for (auto &elem : m_pipeline) {
        auto elem_status = elem->set_nms_score_threshold(threshold);
        if (HAILO_SUCCESS == elem_status) {
            status = elem_status; // 1 element is enough to call this setter successful
        }
    }
    CHECK_SUCCESS(status, "Unable to set NMS score threshold in {}", name());

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamImpl::set_nms_iou_threshold(float32_t threshold)
{
    auto status = HAILO_INVALID_OPERATION; // Assuming there is no valid element
    for (auto &elem : m_pipeline) {
        auto elem_status = elem->set_nms_iou_threshold(threshold);
        if (HAILO_SUCCESS == elem_status) {
            status = elem_status; // 1 element is enough to call this setter successful
        }
    }
    CHECK_SUCCESS(status, "Unable to set NMS IoU threshold in {}", name());

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamImpl::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    auto status = HAILO_INVALID_OPERATION; // Assuming there is no valid element
    std::shared_ptr<UserBufferQueueElement> user_buffer_queue_element = nullptr;
    for (auto &elem : m_pipeline) {
        if (nullptr != std::dynamic_pointer_cast<UserBufferQueueElement>(elem)) {
            user_buffer_queue_element = std::dynamic_pointer_cast<UserBufferQueueElement>(elem);
        }

        auto elem_status = elem->set_nms_max_proposals_per_class(max_proposals_per_class);
        if (HAILO_SUCCESS == elem_status) {
            status = elem_status; // 1 element is enough to call this setter successful

            // Update vstream info and frame size
            m_vstream_info.nms_shape.max_bboxes_per_class = max_proposals_per_class;
            auto set_buffer_size_status = user_buffer_queue_element->set_buffer_pool_buffer_size(HailoRTCommon::get_frame_size(m_vstream_info,
                m_vstream_params.user_buffer_format));
            CHECK_SUCCESS(set_buffer_size_status, "Failed to update buffer size in {}", name());
        }
    }
    CHECK_SUCCESS(status, "Unable to set NMS max proposals per class in {}", name());

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamImpl::set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
{
    auto status = HAILO_INVALID_OPERATION; // Assuming there is no valid element
    std::shared_ptr<UserBufferQueueElement> user_buffer_queue_element = nullptr;
    for (auto &elem : m_pipeline) {
        if (nullptr != std::dynamic_pointer_cast<UserBufferQueueElement>(elem)) {
            user_buffer_queue_element = std::dynamic_pointer_cast<UserBufferQueueElement>(elem);
        }

        auto elem_status = elem->set_nms_max_accumulated_mask_size(max_accumulated_mask_size);
        if (HAILO_SUCCESS == elem_status) {
            status = elem_status; // 1 element is enough to call this setter successful

            // Update vstream info and frame size
            m_vstream_info.nms_shape.max_accumulated_mask_size = max_accumulated_mask_size;
            auto set_buffer_size_status = user_buffer_queue_element->set_buffer_pool_buffer_size(HailoRTCommon::get_frame_size(m_vstream_info,
                m_vstream_params.user_buffer_format));
            CHECK_SUCCESS(set_buffer_size_status, "Failed to update buffer size in {}", name());
        }
    }
    CHECK_SUCCESS(status, "Unable to set NMS max accumulated mask size in {}", name());


    return HAILO_SUCCESS;
}

#ifdef HAILO_SUPPORT_MULTI_PROCESS
Expected<std::shared_ptr<OutputVStreamClient>> OutputVStreamClient::create(const VStreamIdentifier &&identifier, const std::chrono::milliseconds &timeout)
{
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(-1);
    auto channel = grpc::CreateCustomChannel(hailort::HAILORT_SERVICE_ADDRESS, grpc::InsecureChannelCredentials(), ch_args);
    CHECK_AS_EXPECTED(channel != nullptr, HAILO_INTERNAL_FAILURE);

    auto client = make_unique_nothrow<HailoRtRpcClient>(channel);
    CHECK_AS_EXPECTED(client != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto user_buffer_format = client->OutputVStream_get_user_buffer_format(identifier);
    CHECK_EXPECTED(user_buffer_format);

    auto info = client->OutputVStream_get_info(identifier);
    CHECK_EXPECTED(info);

    return std::shared_ptr<OutputVStreamClient>(new OutputVStreamClient(std::move(client), std::move(identifier),
        user_buffer_format.release(), info.release(), timeout));
}

OutputVStreamClient::OutputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, const VStreamIdentifier &&identifier, hailo_format_t &&user_buffer_format,
    hailo_vstream_info_t &&info, const std::chrono::milliseconds &timeout) :
        m_client(std::move(client)), m_identifier(std::move(identifier)), m_user_buffer_format(user_buffer_format), m_info(info), m_timeout(timeout) {}

OutputVStreamClient::~OutputVStreamClient()
{
    auto reply = m_client->OutputVStream_release(m_identifier, OsUtils::get_curr_pid());
    if (reply != HAILO_SUCCESS) {
        LOGGER__CRITICAL("OutputVStream_release failed!");
    }
}

hailo_status OutputVStreamClient::read(MemoryView buffer)
{
    return m_client->OutputVStream_read(m_identifier, buffer, m_timeout);
}

hailo_status OutputVStreamClient::abort()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto abort_client = expected_client.release();
    return abort_client->OutputVStream_abort(m_identifier);
}

hailo_status OutputVStreamClient::resume()
{
    return m_client->OutputVStream_resume(m_identifier);
}

hailo_status OutputVStreamClient::stop_and_clear()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto stop_and_clear_client = expected_client.release();

    return stop_and_clear_client->OutputVStream_stop_and_clear(m_identifier);
}

hailo_status OutputVStreamClient::start_vstream()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto start_vstream_client = expected_client.release();

    return start_vstream_client->OutputVStream_start_vstream(m_identifier);
}

size_t OutputVStreamClient::get_frame_size() const
{
    auto frame_size =  m_client->OutputVStream_get_frame_size(m_identifier);
    if (!frame_size) {
        LOGGER__CRITICAL("OutputVStream_get_frame_size failed with status={}", frame_size.status());
        return 0;
    }
    return frame_size.release();
}

const hailo_vstream_info_t &OutputVStreamClient::get_info() const
{
    return m_info;
}

const hailo_format_t &OutputVStreamClient::get_user_buffer_format() const
{
    return m_user_buffer_format;
}

std::string OutputVStreamClient::name() const
{
    auto expected_name = m_client->OutputVStream_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("OutputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

std::string OutputVStreamClient::network_name() const
{
    auto expected_name = m_client->OutputVStream_network_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("OutputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

const std::map<std::string, AccumulatorPtr> &OutputVStreamClient::get_fps_accumulators() const
{
    LOGGER__ERROR("OutputVStream::get_fps_accumulators function is not supported when using multi-process service");
    return m_fps_accumulators;
}
const std::map<std::string, AccumulatorPtr> &OutputVStreamClient::get_latency_accumulators() const
{
    LOGGER__ERROR("OutputVStream::get_latency_accumulators functoin is not supported when using multi-process service");
    return m_latency_accumulators;
}

const std::map<std::string, std::vector<AccumulatorPtr>> &OutputVStreamClient::get_queue_size_accumulators() const
{
    LOGGER__ERROR("OutputVStream::get_queue_size_accumulators function is not supported when using multi-process service");
    return m_queue_size_accumulators;
}
AccumulatorPtr OutputVStreamClient::get_pipeline_latency_accumulator() const
{
    LOGGER__ERROR("OutputVStream::get_pipeline_latency_accumulator function is not supported when using multi-process service");
    return m_pipeline_latency_accumulator;
}
const std::vector<std::shared_ptr<PipelineElement>> &OutputVStreamClient::get_pipeline() const
{
    LOGGER__ERROR("OutputVStream::get_pipeline function is not supported when using multi-process service");
    return m_pipeline;
}

hailo_status OutputVStreamClient::create_client()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    m_client = expected_client.release();
    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::before_fork()
{
    m_client.reset();
    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::after_fork_in_parent()
{
    return create_client();
}

hailo_status OutputVStreamClient::after_fork_in_child()
{
    return create_client();
}

bool OutputVStreamClient::is_aborted()
{
    auto is_aborted_exp = m_client->OutputVStream_is_aborted(m_identifier);
    if (!is_aborted_exp) {
        LOGGER__CRITICAL("OutputVStream_is_aborted failed with status={}", is_aborted_exp.status());
        return true;
    }
    return is_aborted_exp.release();
}

hailo_status OutputVStreamClient::set_nms_score_threshold(float32_t threshold)
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto vstream_client = expected_client.release();

    CHECK_SUCCESS(vstream_client->OutputVStream_set_nms_score_threshold(m_identifier, threshold));

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::set_nms_iou_threshold(float32_t threshold)
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto vstream_client = expected_client.release();

    CHECK_SUCCESS(vstream_client->OutputVStream_set_nms_iou_threshold(m_identifier, threshold));

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto vstream_client = expected_client.release();

    CHECK_SUCCESS(vstream_client->OutputVStream_set_nms_max_proposals_per_class(m_identifier, max_proposals_per_class));
    m_info.nms_shape.max_bboxes_per_class = max_proposals_per_class;

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto vstream_client = expected_client.release();

    CHECK_SUCCESS(vstream_client->OutputVStream_set_nms_max_accumulated_mask_size(m_identifier, max_accumulated_mask_size));
    m_info.nms_shape.max_accumulated_mask_size = max_accumulated_mask_size;

    return HAILO_SUCCESS;
}

#endif // HAILO_SUPPORT_MULTI_PROCESS

Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> VStreamsBuilder::create_vstreams(
    ConfiguredNetworkGroup &net_group, bool /*unused*/, hailo_format_type_t format_type,
    const std::string &network_name)
{
    const auto params = HailoRTDefaults::get_vstreams_params({}, format_type);
    return create_vstreams(net_group, params, network_name);
}

Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> VStreamsBuilder::create_vstreams(
    ConfiguredNetworkGroup &net_group, const hailo_vstream_params_t &vstreams_params,
    const std::string &network_name)
{
    std::map<std::string, hailo_vstream_params_t> vstreams_params_by_input_stream_name;
    auto input_vstream_params = net_group.make_input_vstream_params(true, HAILO_FORMAT_TYPE_AUTO, 
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_name);
    CHECK_EXPECTED(input_vstream_params);

    for (auto params_pair : input_vstream_params.release()) {
        vstreams_params_by_input_stream_name.emplace(std::make_pair(params_pair.first, vstreams_params));
    }

    auto expected_all_inputs = create_input_vstreams(net_group, vstreams_params_by_input_stream_name);
    CHECK_EXPECTED(expected_all_inputs);

    std::map<std::string, hailo_vstream_params_t> vstreams_params_by_output_stream_name;
    auto output_vstream_params = net_group.make_output_vstream_params(true, HAILO_FORMAT_TYPE_AUTO, 
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_name);
    CHECK_EXPECTED(output_vstream_params);

    for (auto params_pair : output_vstream_params.release()) {
        vstreams_params_by_output_stream_name.emplace(std::make_pair(params_pair.first, vstreams_params));
    }

    auto expected_all_outputs = create_output_vstreams(net_group, vstreams_params_by_output_stream_name);
    CHECK_EXPECTED(expected_all_outputs);

    return std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>(
            expected_all_inputs.release(), expected_all_outputs.release());
}

Expected<std::vector<InputVStream>> VStreamsBuilder::create_input_vstreams(ConfiguredNetworkGroup &net_group,
    const std::map<std::string, hailo_vstream_params_t> &inputs_params)
{
    return net_group.create_input_vstreams(inputs_params);
}

Expected<std::vector<OutputVStream>> VStreamsBuilder::create_output_vstreams(ConfiguredNetworkGroup &net_group,
    const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    return net_group.create_output_vstreams(outputs_params);
}

} /* namespace hailort */
