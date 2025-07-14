/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL 2.1 license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include "gsthailonet.hpp"
#include "tensor_meta.hpp"
#include "hailo/buffer.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"
#include "hailo/hailo_gst_tensor_metadata.hpp"
#include "common.hpp"

#include <algorithm>
#include <unordered_map>

#define WAIT_FOR_ASYNC_READY_TIMEOUT (std::chrono::milliseconds(10000))

enum
{
    PROP_0,
    PROP_HEF_PATH,
    PROP_BATCH_SIZE,
    PROP_DEVICE_ID,
    PROP_DEVICE_COUNT,
    PROP_VDEVICE_GROUP_ID,
    PROP_IS_ACTIVE,
    PROP_OUTPUTS_MIN_POOL_SIZE,
    PROP_OUTPUTS_MAX_POOL_SIZE,
    PROP_SCHEDULING_ALGORITHM,
    PROP_SCHEDULER_TIMEOUT_MS,
    PROP_SCHEDULER_THRESHOLD,
    PROP_SCHEDULER_PRIORITY,
    PROP_INPUT_FORMAT_TYPE,
    PROP_OUTPUT_FORMAT_TYPE,
    PROP_NMS_SCORE_THRESHOLD,
    PROP_NMS_IOU_THRESHOLD,
    PROP_NMS_MAX_PROPOSALS_PER_CLASS,
    PROP_NMS_MAX_PROPOSALS_TOTAL,
    PROP_INPUT_FROM_META,
    PROP_NO_TRANSFORM,
    PROP_MULTI_PROCESS_SERVICE,
    PROP_PASS_THROUGH,
    PROP_FORCE_WRITABLE,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstHailoNet, gst_hailonet, GST_TYPE_ELEMENT);

static void gst_hailonet_dispose(GObject *object) {
    GstHailoNet *self = GST_HAILONET(object);

    assert(nullptr != self->impl);
    delete self->impl;
    self->impl = nullptr;

    G_OBJECT_CLASS(gst_hailonet_parent_class)->dispose(object);
}

static std::atomic_uint32_t hailonet_count(0);

static bool gst_hailo_should_use_dma_buffers()
{
    const char *env = g_getenv(GST_HAILO_USE_DMA_BUFFER_ENV_VAR);
    return (nullptr != env) && (0 == g_strcmp0(env, "1"));
}

static hailo_status gst_hailonet_deconfigure(GstHailoNet *self)
{
    // This will wakeup any blocking calls to deuque
    for (auto &name_pool_pair : self->impl->output_buffer_pools) {
        gst_buffer_pool_set_flushing(name_pool_pair.second, TRUE);
    }

    std::unique_lock<std::mutex> lock(self->impl->infer_mutex);
    self->impl->configured_infer_model.reset();
    self->impl->is_configured = false;
    return HAILO_SUCCESS;
}

static void gst_hailonet_unref_input_caps(GstHailoNet *self)
{
    if (nullptr != self->impl->input_caps) {
        gst_caps_unref(self->impl->input_caps);
        self->impl->input_caps = nullptr;
    }
}

static hailo_status gst_hailonet_free(GstHailoNet *self)
{
    std::unique_lock<std::mutex> lock(self->impl->infer_mutex);
    self->impl->configured_infer_model.reset();
    self->impl->infer_model.reset();
    self->impl->vdevice.reset();

    {
        std::unique_lock<std::mutex> lock2(self->impl->thread_queue_mutex);
        self->impl->is_thread_running = false;
    }
    self->impl->thread_cv.notify_all();

    if (self->impl->thread.joinable()) {
        self->impl->thread.join();
    }

    if (nullptr != self->impl->input_queue) {
        gst_queue_array_free(self->impl->input_queue);
    }

    if (nullptr != self->impl->thread_queue) {
        gst_queue_array_free(self->impl->thread_queue);
    }

    while(!self->impl->curr_event_queue.empty()) {
        auto event = self->impl->curr_event_queue.front();
        gst_event_unref(event);
        self->impl->curr_event_queue.pop();
    }

    for (auto &buffer_events_queue_pair : self->impl->events_queue_per_buffer) {
        while(!buffer_events_queue_pair.second.empty()) {
            auto event = buffer_events_queue_pair.second.front();
            gst_event_unref(event);
            buffer_events_queue_pair.second.pop();
        }
    }
    self->impl->events_queue_per_buffer.clear();

    {
        std::unique_lock<std::mutex> lock3(self->impl->input_caps_mutex);
        gst_hailonet_unref_input_caps(self);
    }

    for (auto &name_pool_pair : self->impl->output_buffer_pools) {
        gboolean result = gst_buffer_pool_set_active(name_pool_pair.second, FALSE);
        CHECK(result, HAILO_INTERNAL_FAILURE, "Could not release buffer pool");
        gst_object_unref(name_pool_pair.second);
    }
    self->impl->output_buffer_pools.clear();

    if (gst_hailo_should_use_dma_buffers()) {
        auto status = self->impl->dmabuf_allocator->close_dma_heap_fd();
        CHECK_SUCCESS(status);

        if (nullptr != self->impl->dmabuf_allocator->impl) {
            gst_object_unref(self->impl->dmabuf_allocator->impl);
        }
    } else if (nullptr != self->impl->allocator) {
        gst_object_unref(self->impl->allocator);
    }

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_set_format_types(GstHailoNet *self, std::shared_ptr<InferModel> infer_model)
{
    if (self->impl->props.m_input_format_type.was_changed()) {
        for (const auto &input_name : infer_model->get_input_names()) {
            TRY(auto input, infer_model->input(input_name));
            input.set_format_type(self->impl->props.m_input_format_type.get());
        }
    }
    if (self->impl->props.m_output_format_type.was_changed()) {
        for (const auto &output_name : infer_model->get_output_names()) {
            TRY(auto output, infer_model->output(output_name));

            output.set_format_type(self->impl->props.m_output_format_type.get());
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_set_nms_params(GstHailoNet *self, std::shared_ptr<InferModel> infer_model)
{
     // Check that if one of the NMS params are changed, we have NMS outputs in the model
    auto has_nms_output = std::any_of(infer_model->outputs().begin(), infer_model->outputs().end(), [](const auto &output)
    {
        return output.is_nms();
    });

    for (const auto &output_name : infer_model->get_output_names()) {
        TRY(auto output, infer_model->output(output_name));

        if (self->impl->props.m_nms_score_threshold.was_changed()) {
            CHECK(has_nms_output, HAILO_INVALID_OPERATION, "NMS score threshold is set, but there is no NMS output in this model.");
            if (output.is_nms()) {
                output.set_nms_score_threshold(self->impl->props.m_nms_score_threshold.get());
            }
        }
        if (self->impl->props.m_nms_iou_threshold.was_changed()) {
            CHECK(has_nms_output, HAILO_INVALID_OPERATION, "NMS IoU threshold is set, but there is no NMS output in this model.");
            if (output.is_nms()) {
                output.set_nms_iou_threshold(self->impl->props.m_nms_iou_threshold.get());
            }
        }
        if (self->impl->props.m_nms_max_proposals_per_class.was_changed()) {
            CHECK(has_nms_output, HAILO_INVALID_OPERATION, "NMS max proposals per class is set, but there is no NMS output in this model.");
            if ((output.is_nms()) && (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE != output.format().order)) {
                output.set_nms_max_proposals_per_class(self->impl->props.m_nms_max_proposals_per_class.get());
            }
        }
        if (self->impl->props.m_nms_max_proposals_total.was_changed()) {
            CHECK(has_nms_output, HAILO_INVALID_OPERATION, "NMS max proposals total is set, but there is no NMS output in this model.");
            if (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE == output.format().order) {
                output.set_nms_max_proposals_total(self->impl->props.m_nms_max_proposals_total.get());
            }
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_set_scheduler_params(GstHailoNet *self, std::shared_ptr<ConfiguredInferModel> configured_infer_model)
{
    if (self->impl->props.m_scheduler_timeout_ms.was_changed()) {
        auto millis = std::chrono::milliseconds(self->impl->props.m_scheduler_timeout_ms.get());
        auto status = configured_infer_model->set_scheduler_timeout(millis);
        CHECK_SUCCESS(status, "Setting scheduler timeout failed, status = %d", status);
    }
    if (self->impl->props.m_scheduler_threshold.was_changed()) {
        auto status = configured_infer_model->set_scheduler_threshold(self->impl->props.m_scheduler_threshold.get());
        CHECK_SUCCESS(status, "Setting scheduler threshold failed, status = %d", status);
    }
    if (self->impl->props.m_scheduler_priority.was_changed()) {
        auto status = configured_infer_model->set_scheduler_priority(self->impl->props.m_scheduler_priority.get());
        CHECK_SUCCESS(status, "Setting scheduler priority failed, status = %d", status);
    }

    return HAILO_SUCCESS;
}

static Expected<GstBufferPool*> gst_hailonet_create_buffer_pool(GstHailoNet *self, size_t frame_size)
{
    GstBufferPool *pool = gst_buffer_pool_new();

    GstStructure *config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, nullptr, static_cast<guint>(frame_size), self->impl->props.m_outputs_min_pool_size.get(),
        self->impl->props.m_outputs_max_pool_size.get());

    if (gst_hailo_should_use_dma_buffers()) {
        gst_buffer_pool_config_set_allocator(config, GST_ALLOCATOR(self->impl->dmabuf_allocator->impl), nullptr);
    } else {
        gst_buffer_pool_config_set_allocator(config, GST_ALLOCATOR(self->impl->allocator), nullptr);
    }

    gboolean result = gst_buffer_pool_set_config(pool, config);
    CHECK_AS_EXPECTED(result, HAILO_INTERNAL_FAILURE, "Could not set config buffer pool");

    result = gst_buffer_pool_set_active(pool, TRUE);
    CHECK_AS_EXPECTED(result, HAILO_INTERNAL_FAILURE, "Could not set buffer pool as active");

    return pool;
}

static void gst_hailonet_push_event_to_queue(GstHailoNet *self, GstEvent *event)
{
    std::unique_lock<std::mutex> lock(self->impl->input_queue_mutex);
    self->impl->curr_event_queue.push(event);
}

static gboolean gst_hailonet_handle_queued_event(GstHailoNet *self, GstEvent *event)
{
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS:
        {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            auto result = gst_pad_set_caps(self->srcpad, caps);
            gst_event_unref(event);
            return result;
        }
        default:
            return gst_pad_push_event(self->srcpad, event);
    }
}

static void gst_hailonet_handle_buffer_events(GstHailoNet *self, GstBuffer *buffer)
{
    if (self->impl->events_queue_per_buffer.find(buffer) == self->impl->events_queue_per_buffer.end()) {
        // The buffer does not have any events to send
        return;
    }

    while (!self->impl->events_queue_per_buffer.at(buffer).empty()) {
        GstEvent* event = self->impl->events_queue_per_buffer.at(buffer).front();
        (void)gst_hailonet_handle_queued_event(self, event);
        self->impl->events_queue_per_buffer.at(buffer).pop();
    }
    self->impl->events_queue_per_buffer.erase(buffer);
}

static hailo_status gst_hailonet_configure(GstHailoNet *self)
{
    if (self->impl->is_configured) {
        return HAILO_SUCCESS;
    }

    for (auto &name_pool_pair : self->impl->output_buffer_pools) {
        gst_buffer_pool_set_flushing(name_pool_pair.second, FALSE);
    }

    self->impl->infer_model->set_batch_size(self->impl->props.m_batch_size.get());

    auto status = gst_hailonet_set_format_types(self, self->impl->infer_model);
    CHECK_SUCCESS(status);

    status = gst_hailonet_set_nms_params(self, self->impl->infer_model);
    CHECK_SUCCESS(status);

    // In RGB formats, Gstreamer is padding each row to 4.
    for (const auto &input_name : self->impl->infer_model->get_input_names()) {
        if(self->impl->props.m_no_transform.get()) {
            // In case transformation is disabled - format order will be the same as we get from the HW (stream info).
            TRY(const auto input_stream_infos, self->impl->infer_model->hef().get_stream_info_by_name(input_name, HAILO_H2D_STREAM));
            self->impl->infer_model->input(input_name)->set_format_order(input_stream_infos.format.order);
        } else if (self->impl->infer_model->input(input_name)->format().order == HAILO_FORMAT_ORDER_NHWC) {
            self->impl->infer_model->input(input_name)->set_format_order(HAILO_FORMAT_ORDER_RGB4);
        }
    }

    if (self->impl->props.m_no_transform.get()) {
        for (const auto &output_name : self->impl->infer_model->get_output_names()) {
            // In case transformation is disabled - format order will be the same as we get from the HW (stream info).
            TRY(const auto output_stream_infos, self->impl->infer_model->hef().get_stream_info_by_name(output_name, HAILO_D2H_STREAM));
            self->impl->infer_model->output(output_name)->set_format_order(output_stream_infos.format.order);
        }
    }

    TRY(auto configured_infer_model, self->impl->infer_model->configure());

    auto ptr = make_shared_nothrow<ConfiguredInferModel>(std::move(configured_infer_model));
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    self->impl->configured_infer_model = ptr;

    status = gst_hailonet_set_scheduler_params(self, self->impl->configured_infer_model);
    CHECK_SUCCESS(status);

    self->impl->is_configured = true;
    return HAILO_SUCCESS;
}

static void gst_hailonet_init_allocator(GstHailoNet *self)
{
    gchar *parent_name = gst_object_get_name(GST_OBJECT(self));
    gchar *name = g_strconcat(parent_name, ":hailo_allocator", NULL);
    g_free(parent_name);

    if (gst_hailo_should_use_dma_buffers()) {
        auto expected_dmabuf_allocator = HailoDmaBuffAllocator::create(name);
        if (HAILO_NOT_IMPLEMENTED == expected_dmabuf_allocator.status()) {
            HAILONET_ERROR("dma buff is not supported on this OS");
        } else if (HAILO_SUCCESS != expected_dmabuf_allocator.status()) {
            HAILONET_ERROR("dma buff creation failed with status %d\n", expected_dmabuf_allocator.status());
        } else {
            self->impl->dmabuf_allocator = expected_dmabuf_allocator.release();
            gst_object_ref_sink(self->impl->dmabuf_allocator->impl);
        }
    } else {
        self->impl->allocator = GST_HAILO_ALLOCATOR(g_object_new(GST_TYPE_HAILO_ALLOCATOR, "name", name, NULL));
        gst_object_ref_sink(self->impl->allocator);
    }

    g_free(name);
}

static hailo_status gst_hailonet_allocate_infer_resources(GstHailoNet *self)
{
    TRY(self->impl->infer_bindings, self->impl->configured_infer_model->create_bindings());

    self->impl->output_buffer_pools = std::unordered_map<std::string, GstBufferPool*>();
    self->impl->output_vstream_infos = std::unordered_map<std::string, hailo_vstream_info_t>();

    TRY(const auto async_queue_size, self->impl->configured_infer_model->get_async_queue_size());
    self->impl->input_queue = gst_queue_array_new(static_cast<guint>(async_queue_size));
    self->impl->thread_queue = gst_queue_array_new(static_cast<guint>(async_queue_size));
    self->impl->is_thread_running = true;
    self->impl->thread = std::thread([self] () {
        while (self->impl->is_thread_running) {
            GstBuffer *buffer = nullptr;
            {
                std::unique_lock<std::mutex> lock(self->impl->thread_queue_mutex);
                self->impl->thread_cv.wait(lock, [self] () {
                    return ((self->impl->buffers_in_thread_queue > 0) || !self->impl->is_thread_running);
                });
                if (!self->impl->is_thread_running) {
                    break;
                }

                buffer = static_cast<GstBuffer*>(gst_queue_array_pop_head(self->impl->thread_queue));
                self->impl->buffers_in_thread_queue--;
            }
            self->impl->thread_cv.notify_all();
            if (GST_IS_PAD(self->srcpad)) { // Checking because we fail here when exiting the application
                GstFlowReturn ret = gst_pad_push(self->srcpad, buffer);
                if ((GST_FLOW_OK != ret) && (GST_FLOW_FLUSHING != ret) && ((GST_FLOW_EOS != ret)) && (!self->impl->has_sent_eos)) {
                    HAILONET_ERROR("gst_pad_push failed with status = %d\n", ret);
                    break;
                }
            }
        }
    });

    gst_hailonet_init_allocator(self);
    for (auto &output : self->impl->infer_model->outputs()) {
        TRY(self->impl->output_buffer_pools[output.name()], gst_hailonet_create_buffer_pool(self, output.get_frame_size()));
    }

    TRY(const auto vstream_infos, self->impl->infer_model->hef().get_output_vstream_infos());
    for (const auto &vstream_info : vstream_infos) {
        self->impl->output_vstream_infos[vstream_info.name] = vstream_info;
    }

    return HAILO_SUCCESS;
}

static GstPadProbeReturn gst_hailonet_sink_probe(GstPad */*pad*/, GstPadProbeInfo */*info*/, gpointer user_data)
{
    GstHailoNet *self = static_cast<GstHailoNet*>(user_data);
    std::unique_lock<std::mutex> lock(self->impl->sink_probe_change_state_mutex);

    if (self->impl->did_critical_failure_happen) {
        return GST_PAD_PROBE_REMOVE;
    }

    auto status = gst_hailonet_configure(self);
    if (HAILO_SUCCESS != status) {
        return GST_PAD_PROBE_REMOVE;
    }

    status = gst_hailonet_allocate_infer_resources(self);
    if (HAILO_SUCCESS != status) {
        return GST_PAD_PROBE_REMOVE;
    }

    if (HAILO_SCHEDULING_ALGORITHM_NONE != self->impl->props.m_scheduling_algorithm.get()) {
        self->impl->props.m_is_active = true;
        return GST_PAD_PROBE_REMOVE;
    }

    if ((1 == hailonet_count) && (!self->impl->props.m_is_active.was_changed())) {
        self->impl->props.m_is_active = true;
    }

    if (self->impl->props.m_is_active.get()) {
        status = self->impl->configured_infer_model->activate();
        if (HAILO_SUCCESS != status) {
            return GST_PAD_PROBE_REMOVE;
        }
    }

    self->impl->has_called_activate = true;
    return GST_PAD_PROBE_REMOVE;
}

static GstStateChangeReturn gst_hailonet_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_hailonet_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        return ret;
    }

    GstHailoNet *self = GST_HAILONET(element);
    std::unique_lock<std::mutex> lock(self->impl->sink_probe_change_state_mutex);

    switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
        auto status = gst_hailonet_configure(self);
        if (HAILO_SUCCESS != status) {
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
        auto status = gst_hailonet_deconfigure(self);
        if (HAILO_SUCCESS != status) {
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
        auto status = gst_hailonet_free(self);
        if (HAILO_SUCCESS != status) {
            return GST_STATE_CHANGE_FAILURE;
        }

        gst_pad_add_probe(self->sinkpad, GST_PAD_PROBE_TYPE_BUFFER, static_cast<GstPadProbeCallback>(gst_hailonet_sink_probe), self, nullptr);
        break;
    }
    default:
        break;
    }

    return ret;
}

static hailo_status gst_hailonet_toggle_activation(GstHailoNet *self, gboolean old_is_active, gboolean new_is_active)
{
    std::unique_lock<std::mutex> lock(self->impl->infer_mutex);

    if (self->impl->props.m_scheduling_algorithm.was_changed() && (HAILO_SCHEDULING_ALGORITHM_NONE != self->impl->props.m_scheduling_algorithm.get())) {
        g_error("scheduling-algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE in combination with 'is-active' is not supported.");
        return HAILO_INVALID_OPERATION;
    }

    if (self->impl->has_called_activate) {
        // Should we keep this? If the user changes the is-active property when we are not configured, it's his fault.
        if (!self->impl->is_configured) {
            g_warning("Trying to change is-active property when network is not configured!");
            return HAILO_INVALID_OPERATION;
        }
        if (old_is_active && !new_is_active) {
            self->impl->configured_infer_model->deactivate();
        } else if (!old_is_active && new_is_active) {
            auto status = self->impl->configured_infer_model->activate();
            CHECK_SUCCESS(status);
        } else {
            g_warning("Trying to change is-active property from %d to %d", old_is_active, new_is_active);
        }
    }

    self->impl->props.m_is_active = new_is_active;
    return HAILO_SUCCESS;
}

static void gst_hailonet_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoNet *self = GST_HAILONET(object);
    switch (property_id) {
    case PROP_HEF_PATH:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the HEF path will not take place!");
            break;
        }
        self->impl->props.m_hef_path = g_value_get_string(value);
        break;
    case PROP_BATCH_SIZE:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the batch size will not take place!");
            break;
        }
        self->impl->props.m_batch_size = static_cast<guint16>(g_value_get_uint(value));
        break;
    case PROP_DEVICE_ID:
        if (0 != self->impl->props.m_device_count.get()) {
            g_error("device-id and device-count excludes eachother. received device-id=%s, device-count=%d",
                g_value_get_string(value), self->impl->props.m_device_count.get());
            break;
        }
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the device ID will not take place!");
            break;
        }
        self->impl->props.m_device_id = g_value_get_string(value);
        break;
    case PROP_DEVICE_COUNT:
        if (!self->impl->props.m_device_id.get().empty()) {
            g_error("device-id and device-count excludes eachother. received device-id=%s, device-count=%d",
                self->impl->props.m_device_id.get().c_str(), g_value_get_uint(value));
            break;
        }
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the device count will not take place!");
            break;
        }
        self->impl->props.m_device_count = static_cast<guint16>(g_value_get_uint(value));
        break;
    case PROP_VDEVICE_GROUP_ID:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the vdevice group ID will not take place!");
            break;
        }
        self->impl->props.m_vdevice_group_id = g_value_get_string(value);
        break;
    case PROP_IS_ACTIVE:
        (void)gst_hailonet_toggle_activation(self, self->impl->props.m_is_active.get(), g_value_get_boolean(value));
        break;
    case PROP_PASS_THROUGH:
        self->impl->props.m_pass_through = g_value_get_boolean(value);
        break;
    case PROP_FORCE_WRITABLE:
        self->impl->props.m_should_force_writable = g_value_get_boolean(value);
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
        if (self->impl->is_configured) {
            g_warning("The network has already been configured, the output's minimum pool size cannot be changed!");
            break;
        }
        self->impl->props.m_outputs_min_pool_size = g_value_get_uint(value);
        break;
    case PROP_OUTPUTS_MAX_POOL_SIZE:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the outputs maximum pool size will not take place!");
            break;
        }
        self->impl->props.m_outputs_max_pool_size = g_value_get_uint(value);
        break;
    case PROP_SCHEDULING_ALGORITHM:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the scheduling algorithm will not take place!");
            break;
        }
        if (self->impl->props.m_is_active.was_changed() && (g_value_get_enum(value) != HAILO_SCHEDULING_ALGORITHM_NONE)) {
            g_error("scheduling-algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE in combination with 'is-active' is not supported.");
            break;
        }
        self->impl->props.m_scheduling_algorithm = static_cast<hailo_scheduling_algorithm_t>(g_value_get_enum(value));
        break;
    case PROP_SCHEDULER_TIMEOUT_MS:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the scheduling timeout will not take place!");
            break;
        }
        self->impl->props.m_scheduler_timeout_ms = g_value_get_uint(value);
        break;
    case PROP_SCHEDULER_THRESHOLD:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the scheduling threshold will not take place!");
            break;
        }
        self->impl->props.m_scheduler_threshold = g_value_get_uint(value);
        break;
    case PROP_SCHEDULER_PRIORITY:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the scheduling priority will not take place!");
            break;
        }
        self->impl->props.m_scheduler_priority = static_cast<guint8>(g_value_get_uint(value));
        break;
    case PROP_INPUT_FORMAT_TYPE:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the format type will not take place!");
            break;
        }
        self->impl->props.m_input_format_type = static_cast<hailo_format_type_t>(g_value_get_enum(value));
        break;
    case PROP_OUTPUT_FORMAT_TYPE:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the format type will not take place!");
            break;
        }
        self->impl->props.m_output_format_type = static_cast<hailo_format_type_t>(g_value_get_enum(value));
        break;
    case PROP_NMS_SCORE_THRESHOLD:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the score threshold will not take place!");
            break;
        }
        self->impl->props.m_nms_score_threshold = static_cast<gfloat>(g_value_get_float(value));
        break;
    case PROP_NMS_IOU_THRESHOLD:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the IoU threshold will not take place!");
            break;
        }
        self->impl->props.m_nms_iou_threshold = static_cast<gfloat>(g_value_get_float(value));
        break;
    case PROP_NMS_MAX_PROPOSALS_PER_CLASS:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the max proposals per class will not take place!");
            break;
        }
        self->impl->props.m_nms_max_proposals_per_class = static_cast<guint32>(g_value_get_uint(value));
        break;
    case PROP_NMS_MAX_PROPOSALS_TOTAL:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the max proposals total will not take place!");
            break;
        }
        self->impl->props.m_nms_max_proposals_total = static_cast<guint32>(g_value_get_uint(value));
        break;
    case PROP_INPUT_FROM_META:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the input method will not take place!");
            break;
        }
        self->impl->props.m_input_from_meta = g_value_get_boolean(value);
        break;
    case PROP_NO_TRANSFORM:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so disabling the transformation will not take place!");
        }
        self->impl->props.m_no_transform = g_value_get_boolean(value);
        break;
    case PROP_MULTI_PROCESS_SERVICE:
        if (self->impl->is_configured) {
            g_warning("The network was already configured so changing the multi-process-service property will not take place!");
            break;
        }
        self->impl->props.m_multi_process_service = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_hailonet_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoNet *self = GST_HAILONET(object);
    switch (property_id) {
    case PROP_HEF_PATH:
        g_value_set_string(value, self->impl->props.m_hef_path.get().c_str());
        break;
    case PROP_BATCH_SIZE:
        g_value_set_uint(value, self->impl->props.m_batch_size.get());
        break;
    case PROP_DEVICE_ID:
        g_value_set_string(value, self->impl->props.m_device_id.get().c_str());
        break;
    case PROP_DEVICE_COUNT:
        g_value_set_uint(value, self->impl->props.m_device_count.get());
        break;
    case PROP_VDEVICE_GROUP_ID:
        g_value_set_string(value, self->impl->props.m_vdevice_group_id.get().c_str());
        break;
    case PROP_IS_ACTIVE:
        g_value_set_boolean(value, self->impl->props.m_is_active.get());
        break;
    case PROP_PASS_THROUGH:
        g_value_set_boolean(value, self->impl->props.m_pass_through.get());
        break;
    case PROP_FORCE_WRITABLE:
        g_value_set_boolean(value, self->impl->props.m_should_force_writable.get());
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
        g_value_set_uint(value, self->impl->props.m_outputs_min_pool_size.get());
        break;
    case PROP_OUTPUTS_MAX_POOL_SIZE:
        g_value_set_uint(value, self->impl->props.m_outputs_max_pool_size.get());
        break;
    case PROP_SCHEDULING_ALGORITHM:
        g_value_set_enum(value, self->impl->props.m_scheduling_algorithm.get());
        break;
    case PROP_SCHEDULER_TIMEOUT_MS:
        g_value_set_uint(value, self->impl->props.m_scheduler_timeout_ms.get());
        break;
    case PROP_SCHEDULER_THRESHOLD:
        g_value_set_uint(value, self->impl->props.m_scheduler_threshold.get());
        break;
    case PROP_SCHEDULER_PRIORITY:
        g_value_set_uint(value, self->impl->props.m_scheduler_priority.get());
        break;
    case PROP_INPUT_FORMAT_TYPE:
        g_value_set_enum(value, self->impl->props.m_input_format_type.get());
        break;
    case PROP_OUTPUT_FORMAT_TYPE:
        g_value_set_enum(value, self->impl->props.m_output_format_type.get());
        break;
    case PROP_NMS_SCORE_THRESHOLD:
        g_value_set_float(value, self->impl->props.m_nms_score_threshold.get());
        break;
    case PROP_NMS_IOU_THRESHOLD:
        g_value_set_float(value, self->impl->props.m_nms_iou_threshold.get());
        break;
    case PROP_NMS_MAX_PROPOSALS_PER_CLASS:
        g_value_set_uint(value, self->impl->props.m_nms_max_proposals_per_class.get());
        break;
    case PROP_NMS_MAX_PROPOSALS_TOTAL:
        g_value_set_uint(value, self->impl->props.m_nms_max_proposals_total.get());
        break;
    case PROP_INPUT_FROM_META:
        g_value_set_boolean(value, self->impl->props.m_input_from_meta.get());
        break;
    case PROP_NO_TRANSFORM:
        g_value_set_boolean(value, self->impl->props.m_no_transform.get());
        break;
    case PROP_MULTI_PROCESS_SERVICE:
        g_value_set_boolean(value, self->impl->props.m_multi_process_service.get());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_hailonet_class_init(GstHailoNetClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->dispose = gst_hailonet_dispose;

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
    element_class->change_state = gst_hailonet_change_state;

    gst_element_class_set_static_metadata(element_class,
        "hailonet element", "Hailo/Network",
        "Configure and Activate Hailo Network. "
            "Supports the \"flush\" signal which blocks until there are no buffers currently processesd in the element. "
            "When deactivating a hailonet during runtime (via set_property of \"is-active\" to False), make sure that no frames are being pushed into the "
            "hailonet, since this operation waits until there are no frames coming in.",
        PLUGIN_AUTHOR);

    gobject_class->set_property = gst_hailonet_set_property;
    gobject_class->get_property = gst_hailonet_get_property;
    g_object_class_install_property(gobject_class, PROP_HEF_PATH,
        g_param_spec_string("hef-path", "HEF Path Location", "Location of the HEF file to read", nullptr,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_BATCH_SIZE,
        g_param_spec_uint("batch-size", "Inference Batch", "How many frame to send in one batch",
            MIN_GSTREAMER_BATCH_SIZE, MAX_GSTREAMER_BATCH_SIZE, HAILO_DEFAULT_BATCH_SIZE,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUTS_MIN_POOL_SIZE,
        g_param_spec_uint("outputs-min-pool-size", "Outputs Minimun Pool Size", "The minimum amount of buffers to allocate for each output layer",
            0, std::numeric_limits<uint32_t>::max(), MIN_OUTPUTS_POOL_SIZE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUTS_MAX_POOL_SIZE,
        g_param_spec_uint("outputs-max-pool-size", "Outputs Maximum Pool Size",
            "The maximum amount of buffers to allocate for each output layer or 0 for unlimited", 0, std::numeric_limits<uint32_t>::max(),
            MAX_OUTPUTS_POOL_SIZE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_DEVICE_ID,
        g_param_spec_string("device-id", "Device ID", "Device ID ([<domain>]:<bus>:<device>.<func>, same as in lspci command). Excludes device-count.", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_DEVICE_COUNT,
        g_param_spec_uint("device-count", "Number of devices to use", "Number of physical devices to use. Excludes device-id.", HAILO_DEFAULT_DEVICE_COUNT,
            std::numeric_limits<uint16_t>::max(), HAILO_DEFAULT_DEVICE_COUNT, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_VDEVICE_GROUP_ID,
        g_param_spec_string("vdevice-group-id",
            "VDevice Group ID to share vdevices across hailonets",
            "Used to share VDevices across different hailonet instances", HAILO_DEFAULT_VDEVICE_GROUP_ID,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // TODO (HRT-12306): Change is-active behavior
    g_object_class_install_property(gobject_class, PROP_IS_ACTIVE,
        g_param_spec_boolean("is-active", "Is Network Activated", "Controls whether this element should be active. "
            "By default, the hailonet element will not be active unless it is the only one. "
            "Setting this property in combination with 'scheduling-algorithm' different than HAILO_SCHEDULING_ALGORITHM_NONE is not supported.", false,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_PASS_THROUGH,
        g_param_spec_boolean("pass-through", "Is element pass-through", "Controls whether the element will perform inference or simply pass buffers through. "
            "By default, the hailonet element will not be pass-through. "
            "Setting this property to true disables inference, regardless of the scheduler settings.", false,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_FORCE_WRITABLE,
        g_param_spec_boolean("force-writable", "Force writable", "Controls whether the element will force the input buffer to be writable. "
            "We force the input to be writable with the function gst_buffer_make_writable, which in most cases will do a shallow copy of the buffer. "
            "But in some cases (when the buffer is marked as not shared - see gst_buffer_copy documentation), it will do a deep copy."
            "By default, the hailonet element will not force the input buffer to be writable and will raise an error when the buffer is read-only.", false,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SCHEDULING_ALGORITHM,
        g_param_spec_enum("scheduling-algorithm", "Scheduling policy for automatic network group switching", "Controls the Model Scheduler algorithm of HailoRT. "
            "Gets values from the enum GstHailoSchedulingAlgorithms. "
            "Using Model Scheduler algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE, excludes the property 'is-active'. "
            "When using the same VDevice across multiple hailonets, all should have the same 'scheduling-algorithm'. ",
            GST_TYPE_SCHEDULING_ALGORITHM, HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_SCHEDULER_TIMEOUT_MS,
        g_param_spec_uint("scheduler-timeout-ms", "Timeout for for scheduler in ms", "The maximum time period that may pass before getting run time from the scheduler,"
            " as long as at least one send request has been sent.",
            HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS, std::numeric_limits<uint32_t>::max(), HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_SCHEDULER_THRESHOLD,
        g_param_spec_uint("scheduler-threshold", "Frames threshold for scheduler", "The minimum number of send requests required before the hailonet is considered ready to get run time from the scheduler.",
            HAILO_DEFAULT_SCHEDULER_THRESHOLD, std::numeric_limits<uint32_t>::max(), HAILO_DEFAULT_SCHEDULER_THRESHOLD, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_SCHEDULER_PRIORITY,
        g_param_spec_uint("scheduler-priority", "Priority index for scheduler", "When the scheduler will choose the next hailonet to run, higher priority will be prioritized in the selection. "
            "Bigger number represent higher priority",
            HAILO_SCHEDULER_PRIORITY_MIN, HAILO_SCHEDULER_PRIORITY_MAX, HAILO_SCHEDULER_PRIORITY_NORMAL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_INPUT_FORMAT_TYPE,
        g_param_spec_enum("input-format-type", "Input format type", "Input format type(auto, float32, uint16, uint8). Default value is auto."
            "Gets values from the enum GstHailoFormatType. ",
            GST_TYPE_HAILO_FORMAT_TYPE, HAILO_FORMAT_TYPE_AUTO,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUT_FORMAT_TYPE,
        g_param_spec_enum("output-format-type", "Output format type", "Output format type(auto, float32, uint16, uint8). Default value is auto."
            "Gets values from the enum GstHailoFormatType. ",
            GST_TYPE_HAILO_FORMAT_TYPE, HAILO_FORMAT_TYPE_AUTO,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_INPUT_FROM_META,
        g_param_spec_boolean("input-from-meta", "Enable input from meta", "Take network input from metadata instead of video frame.", false,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NO_TRANSFORM,
        g_param_spec_boolean("no-transform", "Disable transformations", "Format will remain the same as the HW format.", false,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_NMS_SCORE_THRESHOLD,
        g_param_spec_float("nms-score-threshold", "NMS score threshold", "Threshold used for filtering out candidates. Any box with score<TH is suppressed.",
            0, 1, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NMS_IOU_THRESHOLD,
        g_param_spec_float("nms-iou-threshold", "NMS IoU threshold", "Intersection over union overlap Threshold, used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.",
            0, 1, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NMS_MAX_PROPOSALS_PER_CLASS,
        g_param_spec_uint("nms-max-proposals-per-class", "NMS max proposals per class", "Set a limit for the maximum number of boxes per class.",
            0, std::numeric_limits<uint32_t>::max(), 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NMS_MAX_PROPOSALS_TOTAL,
        g_param_spec_uint("nms-max-proposals-total", "NMS max proposals total", "Set a limit for the maximum number of boxes total.",
        0, std::numeric_limits<uint32_t>::max(), 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_MULTI_PROCESS_SERVICE,
        g_param_spec_boolean("multi-process-service", "Should run over HailoRT service", "Controls wether to run HailoRT over its service. "
            "To use this property, the service should be active and scheduling-algorithm should be set. Defaults to false.",
            HAILO_DEFAULT_MULTI_PROCESS_SERVICE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // See information about the "flush" signal in the element description
    g_signal_new(
        "flush",
        GST_TYPE_HAILONET,
        G_SIGNAL_ACTION,
        0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0
    );
}

static void gst_hailonet_push_buffer_to_thread(GstHailoNet *self, GstBuffer *buffer)
{
    {
        std::unique_lock<std::mutex> lock(self->impl->thread_queue_mutex);
        self->impl->thread_cv.wait(lock, [self] () {
            bool is_unlimited_pool_not_empty = (self->impl->props.m_outputs_max_pool_size.get() == 0) && (self->impl->buffers_in_thread_queue < MAX_OUTPUTS_POOL_SIZE);
            bool is_pool_empty = self->impl->buffers_in_thread_queue < self->impl->props.m_outputs_max_pool_size.get();
            return is_unlimited_pool_not_empty || is_pool_empty;
        });
        gst_queue_array_push_tail(self->impl->thread_queue, buffer);
        self->impl->buffers_in_thread_queue++;
    }
    self->impl->thread_cv.notify_all();
}

// TODO: This function should be refactored. It does many unrelated things and the user need to know that he should unmap the buffer
// in case of an error. AND it does not print errors nor return an indicative status (also the comments are confusing - "continue"?)
static bool set_infos(GstParentBufferMeta *parent_buffer_meta, hailo_tensor_metadata_t &tensor_meta_info, GstMapInfo &info)
{
    gboolean map_succeeded = gst_buffer_map(parent_buffer_meta->buffer, &info, GST_MAP_READ);
    if (!map_succeeded) {
        // Failed to map, this buffer might not have a GstHailoTensorMeta, continue
        return false;
    }
    GstHailoTensorMeta *tensor_meta = GST_TENSOR_META_GET(parent_buffer_meta->buffer);
    if (!tensor_meta) {
        // Not a tensor meta (this buffer is not a tensor), unmap and continue
        gst_buffer_unmap(parent_buffer_meta->buffer, &info);
        return false;
    }
    tensor_meta_info = tensor_meta->info;
    return true;
}

static Expected<std::unordered_map<std::string, hailo_dma_buffer_t>> gst_hailonet_read_input_dma_buffers_from_meta(GstHailoNet *self, GstBuffer *buffer)
{
    std::unordered_map<std::string, hailo_dma_buffer_t> input_buffer_metas;
    gpointer state = nullptr;
    GstMeta *meta = nullptr;

    while (true) {
        meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_PARENT_BUFFER_META_API_TYPE);
        if (!meta) {
            break;
        }
        GstParentBufferMeta *parent_buffer_meta = reinterpret_cast<GstParentBufferMeta*>(meta);
        GstMapInfo info;
        hailo_tensor_metadata_t tensor_meta_info;
        bool result = set_infos(parent_buffer_meta, tensor_meta_info, info);
        if (result) {
            TRY(auto is_dma_buf_memory, HailoDmaBuffAllocator::is_dma_buf_memory(info));
            CHECK_AS_EXPECTED(is_dma_buf_memory, HAILO_INTERNAL_FAILURE, "GstMemory is not a DMA buf as expected!");

            TRY(auto fd, HailoDmaBuffAllocator::memory_get_fd(info));
            CHECK_AS_EXPECTED(fd != -1, HAILO_INTERNAL_FAILURE, "Failed to get FD from GstMemory!");

            hailo_dma_buffer_t dma_buffer = {fd, info.size};
            input_buffer_metas[tensor_meta_info.name] = dma_buffer;
            gst_buffer_unmap(parent_buffer_meta->buffer, &info);
        }
    }
    CHECK_AS_EXPECTED(!input_buffer_metas.empty(),HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer!");

    for (auto &input : self->impl->infer_model->inputs()) {
        CHECK_AS_EXPECTED(input_buffer_metas.find(input.name()) != input_buffer_metas.end(),
            HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer for input: %s", input.name().c_str());
    }

    return input_buffer_metas;
}

static hailo_status gst_hailonet_fill_multiple_input_bindings_dma_buffers(GstHailoNet *self, GstBuffer *buffer)
{
    TRY(auto input_buffers, gst_hailonet_read_input_dma_buffers_from_meta(self, buffer));
    for (const auto &name : self->impl->infer_model->get_input_names()) {
        auto status = self->impl->infer_bindings.input(name)->set_dma_buffer(input_buffers.at(name));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

static Expected<std::unordered_map<std::string, uint8_t*>> gst_hailonet_read_input_buffers_from_meta(GstHailoNet *self, GstBuffer *buffer)
{
    std::unordered_map<std::string, uint8_t*> input_buffer_metas;
    gpointer state = nullptr;
    GstMeta *meta = nullptr;

    while (true) {
        meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_PARENT_BUFFER_META_API_TYPE);
        if (!meta) {
            break;
        }
        GstParentBufferMeta *parent_buffer_meta = reinterpret_cast<GstParentBufferMeta*>(meta);
        GstMapInfo info;
        hailo_tensor_metadata_t tensor_meta_info;
        bool result = set_infos(parent_buffer_meta, tensor_meta_info, info);
        if (result) {
            input_buffer_metas[tensor_meta_info.name] = static_cast<uint8_t*>(info.data);
            gst_buffer_unmap(parent_buffer_meta->buffer, &info);
        }
    }
    CHECK_AS_EXPECTED(!input_buffer_metas.empty(),HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer!");

    for (auto &input : self->impl->infer_model->inputs()) {
        CHECK_AS_EXPECTED(input_buffer_metas.find(input.name()) != input_buffer_metas.end(),
            HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer for input: %s", input.name().c_str());
    }

    return input_buffer_metas;
}

static hailo_status gst_hailonet_fill_multiple_input_bindings(GstHailoNet *self, GstBuffer *buffer)
{
    TRY(auto input_buffers, gst_hailonet_read_input_buffers_from_meta(self, buffer));
    for (const auto &name : self->impl->infer_model->get_input_names()) {
        auto status = self->impl->infer_bindings.input(name)->set_buffer(MemoryView(input_buffers.at(name),
            self->impl->infer_model->input(name)->get_frame_size()));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

static void store_buffer_events(GstHailoNet *self, GstBuffer *buffer)
{
    self->impl->events_queue_per_buffer[buffer] = std::queue<GstEvent*>();
    while (!self->impl->curr_event_queue.empty()) {
        GstEvent *event = self->impl->curr_event_queue.front();
        self->impl->events_queue_per_buffer[buffer].push(event);
        self->impl->curr_event_queue.pop();
    }
}

static hailo_status gst_hailonet_push_buffer_to_input_queue(GstHailoNet *self, GstBuffer *buffer)
{
    std::unique_lock<std::mutex> lock(self->impl->input_queue_mutex);
    store_buffer_events(self, buffer);
    gst_queue_array_push_tail(self->impl->input_queue, buffer);

    return HAILO_SUCCESS;
}

Expected<std::unordered_map<std::string, TensorInfo>> gst_hailonet_fill_output_bindings(GstHailoNet *self)
{
    std::unordered_map<std::string, TensorInfo> tensors;
    for (auto &output : self->impl->infer_model->outputs()) {
        GstBuffer *output_buffer = nullptr;
        GstFlowReturn flow_result = gst_buffer_pool_acquire_buffer(self->impl->output_buffer_pools[output.name()], &output_buffer, nullptr);
        if (GST_FLOW_FLUSHING == flow_result) {
            return make_unexpected(HAILO_STREAM_ABORT);
        } else {
            CHECK_AS_EXPECTED(GST_FLOW_OK == flow_result, HAILO_INTERNAL_FAILURE, "Acquire buffer failed! flow status = %d", flow_result);
        }

        GstMapInfo buffer_info;
        gboolean result = gst_buffer_map(output_buffer, &buffer_info, GST_MAP_WRITE);
        CHECK_AS_EXPECTED(result, HAILO_INTERNAL_FAILURE, "Failed mapping buffer!");

        if (gst_hailo_should_use_dma_buffers()) {
            TRY(auto is_dma_buf_memory, HailoDmaBuffAllocator::is_dma_buf_memory(buffer_info));
            CHECK_AS_EXPECTED(is_dma_buf_memory, HAILO_INTERNAL_FAILURE, "GstMemory is not a DMA buf as expected!");

            TRY(auto fd, HailoDmaBuffAllocator::memory_get_fd(buffer_info));
            CHECK_AS_EXPECTED(fd != -1, HAILO_INTERNAL_FAILURE, "Failed to get FD from GstMemory!");

            hailo_dma_buffer_t dma_buffer = {fd, buffer_info.size};
            auto status = self->impl->infer_bindings.output(output.name())->set_dma_buffer(dma_buffer);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            auto status = self->impl->infer_bindings.output(output.name())->set_buffer(MemoryView(buffer_info.data, buffer_info.size));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }

        tensors[output.name()] = {output_buffer, buffer_info};
    }
    return tensors;
}

static hailo_status gst_hailonet_fill_single_input_binding(GstHailoNet *self, hailo_pix_buffer_t pix_buffer)
{
    auto status = self->impl->infer_bindings.input()->set_pix_buffer(pix_buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_call_run_async(GstHailoNet *self, const std::unordered_map<std::string, TensorInfo> &tensors)
{
    auto status = self->impl->configured_infer_model->wait_for_async_ready(WAIT_FOR_ASYNC_READY_TIMEOUT);
    CHECK_SUCCESS(status);

    {
        std::unique_lock<std::mutex> lock(self->impl->flush_mutex);
        self->impl->ongoing_frames++;
    }

    TRY(auto job, self->impl->configured_infer_model->run_async(self->impl->infer_bindings, [self, tensors] (const AsyncInferCompletionInfo &/*completion_info*/) {
        GstBuffer *buffer = nullptr;
        {
            std::unique_lock<std::mutex> lock(self->impl->input_queue_mutex);
            buffer = static_cast<GstBuffer*>(gst_queue_array_pop_head(self->impl->input_queue));
            gst_hailonet_handle_buffer_events(self, buffer);
        }

        for (auto &output : self->impl->infer_model->outputs()) {
            auto info = tensors.at(output.name());
            gst_buffer_unmap(info.buffer, &info.buffer_info);

            GstHailoTensorMeta *buffer_meta = GST_TENSOR_META_ADD(info.buffer);
            buffer_meta->info = tensor_metadata_from_vstream_info(self->impl->output_vstream_infos[output.name()]);

            (void)gst_buffer_add_parent_buffer_meta(buffer, info.buffer);
            gst_buffer_unref(info.buffer);
        }

        {
            std::unique_lock<std::mutex> lock(self->impl->flush_mutex);
            self->impl->ongoing_frames--;
        }
        self->impl->flush_cv.notify_all();

        gst_hailonet_push_buffer_to_thread(self, buffer);

        if (self->impl->has_pending_eos) {
            bool is_last_frame = false;
            {
                std::unique_lock<std::mutex> lock(self->impl->flush_mutex);
                if (0 == self->impl->ongoing_frames) {
                    is_last_frame = true;
                }
            }

            if (is_last_frame) {
                self->impl->has_sent_eos = true;
                auto event = gst_event_new_eos();
                (void)gst_pad_push_event(self->srcpad, event);
            }
        }
    }));
    job.detach();

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_async_infer_multi_input(GstHailoNet *self, GstBuffer *buffer)
{
    if (gst_hailo_should_use_dma_buffers()) {
        auto status = gst_hailonet_fill_multiple_input_bindings_dma_buffers(self, buffer);
        CHECK_SUCCESS(status);
    } else {
        auto status = gst_hailonet_fill_multiple_input_bindings(self, buffer);
        CHECK_SUCCESS(status);
    }

    auto status = gst_hailonet_push_buffer_to_input_queue(self, buffer);
    CHECK_SUCCESS(status);

    auto tensors = gst_hailonet_fill_output_bindings(self);
    if (HAILO_STREAM_ABORT == tensors.status()) {
        return HAILO_SUCCESS;
    }
    CHECK_EXPECTED_AS_STATUS(tensors); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    status = gst_hailonet_call_run_async(self, tensors.value());
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_async_infer_single_input(GstHailoNet *self, GstBuffer * buffer, hailo_pix_buffer_t pix_buffer)
{
    auto status = gst_hailonet_fill_single_input_binding(self, pix_buffer);
    CHECK_SUCCESS(status);

    status = gst_hailonet_push_buffer_to_input_queue(self, buffer);
    CHECK_SUCCESS(status);

    auto tensors = gst_hailonet_fill_output_bindings(self);
    if (HAILO_STREAM_ABORT == tensors.status()) {
        return HAILO_SUCCESS;
    }
    CHECK_EXPECTED_AS_STATUS(tensors); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    status = gst_hailonet_call_run_async(self, tensors.value());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

uint32_t get_frame_width(const GstVideoFrame *frame, uint32_t plane_index)
{
    switch (frame->info.finfo->format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_I420:
        /* On multi-planar formats, GStreamer can add padding to plane's width without any way to know the padding size,
            so we use the original width set by the caps */
        return frame->info.width;
    default:
        return GST_VIDEO_INFO_PLANE_STRIDE(&(frame->info), plane_index);
    }
}

static Expected<hailo_pix_buffer_t> gst_hailonet_construct_pix_buffer(GstHailoNet *self, GstBuffer *buffer)
{
    GstVideoFrame frame;
    auto result = gst_video_frame_map(&frame, &self->impl->input_frame_info, buffer,
        static_cast<GstMapFlags>(GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF));
    CHECK_AS_EXPECTED(result,HAILO_INTERNAL_FAILURE, "gst_video_frame_map failed!");

    hailo_pix_buffer_t pix_buffer = {};
    pix_buffer.index = 0;
    pix_buffer.number_of_planes = GST_VIDEO_INFO_N_PLANES(&frame.info);
    pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR;

    for (uint32_t plane_index = 0; plane_index < pix_buffer.number_of_planes; plane_index++) {
        pix_buffer.planes[plane_index].bytes_used = get_frame_width(&frame, plane_index) * GST_VIDEO_INFO_COMP_HEIGHT(&frame.info, plane_index);
        pix_buffer.planes[plane_index].plane_size = pix_buffer.planes[plane_index].bytes_used;
        pix_buffer.planes[plane_index].user_ptr = GST_VIDEO_FRAME_PLANE_DATA(&frame, plane_index);
    }

    gst_video_frame_unmap(&frame);
    return pix_buffer;
}

static GstFlowReturn gst_hailonet_chain(GstPad * /*pad*/, GstObject * parent, GstBuffer * buffer)
{
    GstHailoNet *self = GST_HAILONET(parent);
    std::unique_lock<std::mutex> lock(self->impl->infer_mutex);

    if (self->impl->did_critical_failure_happen) {
        return GST_FLOW_ERROR;
    }

    if (self->impl->props.m_pass_through.get() || !self->impl->props.m_is_active.get() || !self->impl->is_configured) {
        store_buffer_events(self, buffer);
        gst_hailonet_handle_buffer_events(self, buffer);
        gst_hailonet_push_buffer_to_thread(self, buffer);
        return GST_FLOW_OK;
    }

    if (!gst_buffer_is_writable(buffer)) {
        if (self->impl->props.m_should_force_writable.get()) {
            buffer = gst_buffer_make_writable(buffer);
            if (nullptr == buffer) {
                HAILONET_ERROR("Failed to make buffer writable!\n");
                return GST_FLOW_ERROR;
            }
        } else {
            HAILONET_ERROR("Input buffer is not writable! Use force-writable property to force the buffer to be writable\n");
            return GST_FLOW_ERROR;
        }
    }

    if (self->impl->props.m_input_from_meta.get()) {
        auto status = gst_hailonet_async_infer_multi_input(self, buffer);
        if (HAILO_SUCCESS != status) {
            return GST_FLOW_ERROR;
        }
    } else {
        auto pix_buffer = gst_hailonet_construct_pix_buffer(self, buffer);
        if (!pix_buffer) {
            return GST_FLOW_ERROR;
        }
        auto status = gst_hailonet_async_infer_single_input(self, buffer, pix_buffer.value());
        if (HAILO_SUCCESS != status) {
            return GST_FLOW_ERROR;
        }
    }

    return GST_FLOW_OK;
}

static hailo_status gst_hailonet_init_infer_model(GstHailoNet * self)
{
    auto vdevice_params = HailoRTDefaults::get_vdevice_params();

    hailo_device_id_t device_id = {0};
    if (self->impl->props.m_device_id.was_changed()) {
        TRY(device_id, HailoRTCommon::to_device_id(self->impl->props.m_device_id.get()));
        vdevice_params.device_ids = &device_id;
    }
    if (self->impl->props.m_device_count.was_changed()) {
        vdevice_params.device_count = self->impl->props.m_device_count.get();
    }
    if (self->impl->props.m_vdevice_group_id.was_changed()) {
        vdevice_params.group_id = self->impl->props.m_vdevice_group_id.get().c_str();
    }
    if (self->impl->props.m_scheduling_algorithm.was_changed()) {
        vdevice_params.scheduling_algorithm = self->impl->props.m_scheduling_algorithm.get();
    }
    if (self->impl->props.m_multi_process_service.was_changed()) {
        vdevice_params.multi_process_service = self->impl->props.m_multi_process_service.get();
        CHECK(self->impl->props.m_scheduling_algorithm.get() != HAILO_SCHEDULING_ALGORITHM_NONE, HAILO_INVALID_OPERATION,
            "To use multi-process-service please set scheduling-algorithm to a value other than 'none'");
    }

    TRY(self->impl->vdevice, VDevice::create(vdevice_params));
    TRY(self->impl->infer_model, self->impl->vdevice->create_infer_model(self->impl->props.m_hef_path.get()));

    if(!(self->impl->props.m_input_from_meta.get())){
        CHECK(self->impl->infer_model->inputs().size() == 1, HAILO_INVALID_OPERATION,
            "In case you want to run a multiple input model, please set the input-from-meta flag.");
    }

    return HAILO_SUCCESS;
}

static const gchar *gst_hailonet_get_format_string(const InferModel::InferStream &input)
{
    switch (input.format().order) {
    case HAILO_FORMAT_ORDER_RGB4:
    case HAILO_FORMAT_ORDER_NHWC:
        if (input.shape().features == RGBA_FEATURES_SIZE) {
            return "RGBA";
        }
        if (input.shape().features == GRAY8_FEATURES_SIZE) {
            return "GRAY8";
        }
        /* Fallthrough */
    case HAILO_FORMAT_ORDER_NHCW:
    case HAILO_FORMAT_ORDER_FCR:
    case HAILO_FORMAT_ORDER_F8CR:
        if (input.shape().features == GRAY8_FEATURES_SIZE) {
            return "GRAY8";
        }
        CHECK(RGB_FEATURES_SIZE == input.shape().features, nullptr,
            "Features of input %s is not %d for RGB format! (features=%d)", input.name().c_str(), RGB_FEATURES_SIZE,
            input.shape().features);
        return "RGB";
    case HAILO_FORMAT_ORDER_YUY2:
        CHECK(YUY2_FEATURES_SIZE == input.shape().features, nullptr,
            "Features of input %s is not %d for YUY2 format! (features=%d)", input.name().c_str(), YUY2_FEATURES_SIZE,
            input.shape().features);
        return "YUY2";
    case HAILO_FORMAT_ORDER_NV12:
        CHECK(NV12_FEATURES_SIZE == input.shape().features, nullptr,
            "Features of input %s is not %d for NV12 format! (features=%d)", input.name().c_str(), NV12_FEATURES_SIZE,
            input.shape().features);
        return "NV12";
    case HAILO_FORMAT_ORDER_NV21:
        CHECK(NV21_FEATURES_SIZE == input.shape().features, nullptr,
            "Features of input %s is not %d for NV21 format! (features=%d)", input.name().c_str(), NV21_FEATURES_SIZE,
            input.shape().features);
        return "NV21";
    case HAILO_FORMAT_ORDER_I420:
        CHECK(I420_FEATURES_SIZE == input.shape().features, nullptr,
            "Features of input %s is not %d for I420 format! (features=%d)", input.name().c_str(), I420_FEATURES_SIZE,
            input.shape().features);
        return "I420";
    default:
        HAILONET_ERROR("Input %s has an unsupported format order! order = %d\n", input.name().c_str(), input.format().order);
        return nullptr;
    }
}

static uint32_t get_height_by_order(uint32_t original_height, hailo_format_order_t order)
{
    switch (order) {
    case HAILO_FORMAT_ORDER_NV12:
    case HAILO_FORMAT_ORDER_NV21:
        return original_height * 2;
    default:
        break;
    }
    return original_height;
}

static GstCaps *gst_hailonet_get_caps(GstHailoNet *self)
{
    if (self->impl->did_critical_failure_happen) {
        // Sometimes gst_hailonet_get_caps will get called again even after a critical failure happened and nullptr was returned
        return nullptr;
    }

    if (nullptr != self->impl->input_caps) {
        return gst_caps_copy(self->impl->input_caps);
    }

    if (nullptr == self->impl->vdevice) {
        auto status = gst_hailonet_init_infer_model(self);
        if (HAILO_SUCCESS != status) {
            self->impl->did_critical_failure_happen = true;
            return nullptr;
        }
    }

    if (self->impl->props.m_input_from_meta.get()) {
        GstCaps *new_caps = gst_caps_new_any();
        std::unique_lock<std::mutex> lock(self->impl->input_caps_mutex);
        gst_hailonet_unref_input_caps(self);
        self->impl->input_caps = new_caps;
        return gst_caps_copy(new_caps);
    }

    auto input = self->impl->infer_model->input();
    if (!input) {
        HAILONET_ERROR("Getting input has failed with status = %d\n", input.status());
        return nullptr;
    }

    const gchar *format = gst_hailonet_get_format_string(input.value());
    if (nullptr == format) {
        return nullptr;
    }

    GstCaps *new_caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, format,
        "width", G_TYPE_INT, input->shape().width,
        "height", G_TYPE_INT, get_height_by_order(input->shape().height, input->format().order),
        nullptr);

    if (!gst_video_info_from_caps(&self->impl->input_frame_info, new_caps)) {
        HAILONET_ERROR("gst_video_info_from_caps failed\n");
        return nullptr;
    }

    std::unique_lock<std::mutex> lock(self->impl->input_caps_mutex);
    gst_hailonet_unref_input_caps(self);
    self->impl->input_caps = new_caps;
    return gst_caps_copy(new_caps);
}

static gboolean gst_hailonet_handle_sink_query(GstPad * pad, GstObject * parent, GstQuery * query)
{
    GstHailoNet *self = GST_HAILONET(parent);
    switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
        GstCaps *caps = gst_hailonet_get_caps(self);
        if (nullptr == caps) {
            return FALSE;
        }
        gst_query_set_caps_result(query, caps);
        gst_caps_unref(caps);
        return TRUE;
    }
    case GST_QUERY_ALLOCATION:
    {
        // We implement this to make sure buffers are contiguous in memory
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
        return gst_pad_query_default(pad, parent, query);
    }
    default:
        return gst_pad_query_default(pad, parent, query);
    }
}

static gboolean gst_hailonet_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    GstHailoNet *self = GST_HAILONET(parent);
    if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
        // We want to forward EOS event only after all the frames have been processed (see callback of run_async)
        self->impl->has_pending_eos = true;
        return TRUE;
    }
    if (GST_EVENT_IS_STICKY(event)) {
        gst_hailonet_push_event_to_queue(self, event);
        return TRUE;
    } else {
        return gst_pad_event_default(pad, parent, event);
    }
}

static void gst_hailonet_flush_callback(GstHailoNet *self, gpointer /*data*/)
{
    std::unique_lock<std::mutex> lock(self->impl->flush_mutex);
    self->impl->flush_cv.wait(lock, [self] () {
        return 0 == self->impl->ongoing_frames;
    });
}

HailoNetImpl::HailoNetImpl() :
    events_queue_per_buffer(), curr_event_queue(), input_queue(nullptr), thread_queue(nullptr), buffers_in_thread_queue(0),
    props(), input_caps(nullptr), is_thread_running(false), has_pending_eos(false), has_sent_eos(false),
    did_critical_failure_happen(false), vdevice(nullptr), is_configured(false), has_called_activate(false), ongoing_frames(0)
{}

Expected<std::unique_ptr<HailoNetImpl>> HailoNetImpl::create()
{
    auto ptr = make_unique_nothrow<HailoNetImpl>();
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

static void gst_hailonet_init(GstHailoNet *self)
{
    if (!do_versions_match(GST_ELEMENT(self))) {
        return;
    }

    self->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_chain_function(self->sinkpad, gst_hailonet_chain);
    gst_pad_set_query_function(self->sinkpad, gst_hailonet_handle_sink_query);
    gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_hailonet_sink_event));
    gst_element_add_pad(GST_ELEMENT (self), self->sinkpad);
    gst_pad_add_probe(self->sinkpad, GST_PAD_PROBE_TYPE_BUFFER, static_cast<GstPadProbeCallback>(gst_hailonet_sink_probe), self, nullptr);

    self->srcpad = gst_pad_new_from_static_template(&src_template, "src");
    gst_element_add_pad(GST_ELEMENT (self), self->srcpad);

    auto hailonet_impl = HailoNetImpl::create();
    if (!hailonet_impl) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Creating hailonet implementation has failed! status = %d", hailonet_impl.status()), (NULL));
        return;
    }

    self->impl = hailonet_impl->release();

    g_signal_connect(self, "flush", G_CALLBACK(gst_hailonet_flush_callback), nullptr);

    hailonet_count++;
}
