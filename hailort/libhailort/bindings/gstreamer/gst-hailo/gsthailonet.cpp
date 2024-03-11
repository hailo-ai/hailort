/*
 * Copyright (c) 2021-2023 Hailo Technologies Ltd. All rights reserved.
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
#include "metadata/tensor_meta.hpp"
#include "hailo/buffer.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"

#include <gst/allocators/gstdmabuf.h>
#include <algorithm>
#include <unordered_map>

#define WAIT_FOR_ASYNC_READY_TIMEOUT (std::chrono::milliseconds(10000))
#define ERROR(msg, ...) g_print(msg, ##__VA_ARGS__)

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
    PROP_INPUT_FROM_META,
    PROP_NO_TRANSFORM,
    PROP_MULTI_PROCESS_SERVICE,
    PROP_PASS_THROUGH,

    // Deprecated
    PROP_VDEVICE_KEY,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstHailoAllocator, gst_hailo_allocator, GST_TYPE_ALLOCATOR);
G_DEFINE_TYPE (GstHailoNet, gst_hailonet, GST_TYPE_ELEMENT);

static std::atomic_uint32_t hailonet_count(0);

static bool gst_hailo_should_use_dma_buffers()
{
    const char *env = g_getenv(GST_HAILO_USE_DMA_BUFFER_ENV_VAR);
    return (nullptr != env) && (0 == g_strcmp0(env, "1"));
}

static GstMemory *gst_hailo_allocator_alloc(GstAllocator* allocator, gsize size, GstAllocationParams* /*params*/) {
    GstHailoAllocator *hailo_allocator = GST_HAILO_ALLOCATOR(allocator);
    auto buffer = Buffer::create(size, BufferStorageParams::create_dma());
    if (!buffer) {
        ERROR("Creating buffer for allocator has failed, status = %d\n", buffer.status());
        return nullptr;
    }

    GstMemory *memory = gst_memory_new_wrapped(static_cast<GstMemoryFlags>(0), buffer->data(),
        buffer->size(), 0, buffer->size(), nullptr, nullptr);
    if (nullptr == memory) {
        ERROR("Creating new GstMemory for allocator has failed!\n");
        return nullptr;
    }

    hailo_allocator->buffers[memory] = std::move(buffer.release());
    return memory;
}

static void gst_hailo_allocator_free(GstAllocator* allocator, GstMemory *mem) {
    GstHailoAllocator *hailo_allocator = GST_HAILO_ALLOCATOR(allocator);
    hailo_allocator->buffers.erase(mem);
}

static void gst_hailo_allocator_class_init(GstHailoAllocatorClass* klass) {
    GstAllocatorClass* allocator_class = GST_ALLOCATOR_CLASS(klass);

    allocator_class->alloc = gst_hailo_allocator_alloc;
    allocator_class->free = gst_hailo_allocator_free;
}

static void gst_hailo_allocator_init(GstHailoAllocator* allocator) {
    allocator->buffers = std::unordered_map<GstMemory*, Buffer>();
}

static hailo_status gst_hailonet_deconfigure(GstHailoNet *self)
{
    // This will wakeup any blocking calls to deuque
    for (auto &name_pool_pair : self->output_buffer_pools) {
        gst_buffer_pool_set_flushing(name_pool_pair.second, TRUE);
    }

    std::unique_lock<std::mutex> lock(self->infer_mutex);
    self->configured_infer_model.reset();
    self->is_configured = false;
    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_free(GstHailoNet *self)
{
    std::unique_lock<std::mutex> lock(self->infer_mutex);
    self->configured_infer_model.reset();
    self->infer_model.reset();
    self->vdevice.reset();

    {
        std::unique_lock<std::mutex> lock(self->thread_queue_mutex);
        self->is_thread_running = false;
    }
    self->thread_cv.notify_all();

    if (self->thread.joinable()) {
        self->thread.join();
    }

    if (nullptr != self->input_queue) {
        gst_queue_array_free(self->input_queue);
    }

    if (nullptr != self->thread_queue) {
        gst_queue_array_free(self->thread_queue);
    }

    if (nullptr != self->input_caps) {
        gst_caps_unref(self->input_caps);
    }

    for (auto &name_pool_pair : self->output_buffer_pools) {
        gboolean result = gst_buffer_pool_set_active(name_pool_pair.second, FALSE);
        CHECK(result, HAILO_INTERNAL_FAILURE, "Could not release buffer pool");
        gst_object_unref(name_pool_pair.second);
    }
    if (gst_hailo_should_use_dma_buffers()) {
        gst_object_unref(self->dma_allocator);
    } else {
        gst_object_unref(self->allocator);
    }

    self->props.free_strings();

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_set_format_types(GstHailoNet *self, std::shared_ptr<InferModel> infer_model)
{
    if (self->props.m_input_format_type.was_changed()) {
        for (const auto &input_name : infer_model->get_input_names()) {
            auto input = infer_model->input(input_name);
            CHECK_EXPECTED_AS_STATUS(input);

            input->set_format_type(self->props.m_input_format_type.get());
        }
    }
    if (self->props.m_output_format_type.was_changed()) {
        for (const auto &output_name : infer_model->get_output_names()) {
            auto output = infer_model->output(output_name);
            CHECK_EXPECTED_AS_STATUS(output);

            output->set_format_type(self->props.m_output_format_type.get());
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
        auto output = infer_model->output(output_name);
        CHECK_EXPECTED_AS_STATUS(output);

        if (self->props.m_nms_score_threshold.was_changed()) {
            CHECK(has_nms_output, HAILO_INVALID_OPERATION, "NMS score threshold is set, but there is no NMS output in this model.");
            if (output->is_nms()) {
                output->set_nms_score_threshold(self->props.m_nms_score_threshold.get());
            }
        }
        if (self->props.m_nms_iou_threshold.was_changed()) {
            CHECK(has_nms_output, HAILO_INVALID_OPERATION, "NMS IoU threshold is set, but there is no NMS output in this model.");
            if (output->is_nms()) {
                output->set_nms_iou_threshold(self->props.m_nms_iou_threshold.get());
            }
        }
        if (self->props.m_nms_max_proposals_per_class.was_changed()) {
            CHECK(has_nms_output, HAILO_INVALID_OPERATION, "NMS max proposals per class is set, but there is no NMS output in this model.");
            if (output->is_nms()) {
                output->set_nms_max_proposals_per_class(self->props.m_nms_max_proposals_per_class.get());
            }
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_set_scheduler_params(GstHailoNet *self, std::shared_ptr<ConfiguredInferModel> configured_infer_model)
{
    if (self->props.m_scheduler_timeout_ms.was_changed()) {
        auto millis = std::chrono::milliseconds(self->props.m_scheduler_timeout_ms.get());
        auto status = configured_infer_model->set_scheduler_timeout(millis);
        CHECK_SUCCESS(status, "Setting scheduler timeout failed, status = %d", status);
    }
    if (self->props.m_scheduler_threshold.was_changed()) {
        auto status = configured_infer_model->set_scheduler_threshold(self->props.m_scheduler_threshold.get());
        CHECK_SUCCESS(status, "Setting scheduler threshold failed, status = %d", status);
    }
    if (self->props.m_scheduler_priority.was_changed()) {
        auto status = configured_infer_model->set_scheduler_priority(self->props.m_scheduler_priority.get());
        CHECK_SUCCESS(status, "Setting scheduler priority failed, status = %d", status);
    }

    return HAILO_SUCCESS;
}

static Expected<GstBufferPool*> gst_hailonet_create_buffer_pool(GstHailoNet *self, size_t frame_size)
{
    GstBufferPool *pool = gst_buffer_pool_new();

    GstStructure *config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, nullptr, static_cast<guint>(frame_size), self->props.m_outputs_min_pool_size.get(),
        self->props.m_outputs_max_pool_size.get());

    if (gst_hailo_should_use_dma_buffers()) {
        gst_buffer_pool_config_set_allocator(config, self->dma_allocator, nullptr);
    } else {
        gst_buffer_pool_config_set_allocator(config, GST_ALLOCATOR(self->allocator), nullptr);
    }

    gboolean result = gst_buffer_pool_set_config(pool, config);
    CHECK_AS_EXPECTED(result, HAILO_INTERNAL_FAILURE, "Could not set config buffer pool");

    result = gst_buffer_pool_set_active(pool, TRUE);
    CHECK_AS_EXPECTED(result, HAILO_INTERNAL_FAILURE, "Could not set buffer pool as active");

    return pool;
}

static hailo_status gst_hailonet_configure(GstHailoNet *self)
{
    if (self->is_configured) {
        return HAILO_SUCCESS;
    }

    for (auto &name_pool_pair : self->output_buffer_pools) {
        gst_buffer_pool_set_flushing(name_pool_pair.second, FALSE);
    }

    self->infer_model->set_batch_size(self->props.m_batch_size.get());

    auto status = gst_hailonet_set_format_types(self, self->infer_model);
    CHECK_SUCCESS(status);

    status = gst_hailonet_set_nms_params(self, self->infer_model);
    CHECK_SUCCESS(status);

    // In RGB formats, Gstreamer is padding each row to 4.
    for (const auto &input_name : self->infer_model->get_input_names()) {
        if(self->props.m_no_transform.get()) {
            // In case transformation is disabled - format order will be the same as we get from the HW (stream info).
            auto input_stream_infos = self->infer_model->hef().get_stream_info_by_name(input_name, HAILO_H2D_STREAM);
            CHECK_EXPECTED_AS_STATUS(input_stream_infos);
            self->infer_model->input(input_name)->set_format_order(input_stream_infos.value().format.order);
        } else if (self->infer_model->input(input_name)->format().order == HAILO_FORMAT_ORDER_NHWC) {
            self->infer_model->input(input_name)->set_format_order(HAILO_FORMAT_ORDER_RGB4);
        }
    }

    if (self->props.m_no_transform.get()) {
        for (const auto &output_name : self->infer_model->get_output_names()) {
            // In case transformation is disabled - format order will be the same as we get from the HW (stream info).
            auto output_stream_infos = self->infer_model->hef().get_stream_info_by_name(output_name, HAILO_D2H_STREAM);
            CHECK_EXPECTED_AS_STATUS(output_stream_infos);
            self->infer_model->output(output_name)->set_format_order(output_stream_infos.value().format.order);
        }
    }

    auto configured_infer_model = self->infer_model->configure();
    CHECK_EXPECTED_AS_STATUS(configured_infer_model);

    auto ptr = make_shared_nothrow<ConfiguredInferModel>(configured_infer_model.release());
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    self->configured_infer_model = ptr;

    status = gst_hailonet_set_scheduler_params(self, self->configured_infer_model);
    CHECK_SUCCESS(status);

    self->is_configured = true;
    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_allocate_infer_resources(GstHailoNet *self)
{
    auto bindings = self->configured_infer_model->create_bindings();
    CHECK_EXPECTED_AS_STATUS(bindings);
    self->infer_bindings = std::move(bindings.release());

    self->output_buffer_pools = std::unordered_map<std::string, GstBufferPool*>();
    self->output_vstream_infos = std::unordered_map<std::string, hailo_vstream_info_t>();

    auto async_queue_size = self->configured_infer_model->get_async_queue_size();
    CHECK_EXPECTED_AS_STATUS(async_queue_size);
    self->input_queue = gst_queue_array_new(static_cast<guint>(async_queue_size.value()));
    self->thread_queue = gst_queue_array_new(static_cast<guint>(async_queue_size.value()));
    self->is_thread_running = true;
    self->thread = std::thread([self] () {
        while (self->is_thread_running) {
            GstBuffer *buffer = nullptr;
            {
                std::unique_lock<std::mutex> lock(self->thread_queue_mutex);
                self->thread_cv.wait(lock, [self] () {
                    return (self->buffers_in_thread_queue > 0) || !self->is_thread_running;
                });
                if (!self->is_thread_running) {
                    break;
                }

                buffer = static_cast<GstBuffer*>(gst_queue_array_pop_head(self->thread_queue));
                self->buffers_in_thread_queue--;
            }
            self->thread_cv.notify_all();
            if (GST_IS_PAD(self->srcpad)) { // Checking because we fail here when exiting the application
                GstFlowReturn ret = gst_pad_push(self->srcpad, buffer);
                if ((GST_FLOW_OK != ret) && (GST_FLOW_FLUSHING != ret) && (!self->has_got_eos)) {
                    ERROR("gst_pad_push failed with status = %d\n", ret);
                    break;
                }
            }
        }
    });

    for (auto &output : self->infer_model->outputs()) {
        auto buffer_pool = gst_hailonet_create_buffer_pool(self, output.get_frame_size());
        CHECK_EXPECTED_AS_STATUS(buffer_pool);

        self->output_buffer_pools[output.name()] = buffer_pool.release();
    }

    auto vstream_infos = self->infer_model->hef().get_output_vstream_infos();
    CHECK_EXPECTED_AS_STATUS(vstream_infos);

    for (const auto &vstream_info : vstream_infos.value()) {
        self->output_vstream_infos[vstream_info.name] = vstream_info;
    }

    return HAILO_SUCCESS;
}

static GstStateChangeReturn gst_hailonet_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_hailonet_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        return ret;
    }

    GstHailoNet *self = GST_HAILONET(element);
    std::unique_lock<std::mutex> lock(self->sink_probe_change_state_mutex);

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
        break;
    }
    default:
        break;
    }

    return ret;
}

static hailo_status gst_hailonet_toggle_activation(GstHailoNet *self, gboolean old_is_active, gboolean new_is_active)
{
    std::unique_lock<std::mutex> lock(self->infer_mutex);

    if (self->props.m_scheduling_algorithm.was_changed() && (HAILO_SCHEDULING_ALGORITHM_NONE != self->props.m_scheduling_algorithm.get())) {
        g_error("scheduling-algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE in combination with 'is-active' is not supported.");
        return HAILO_INVALID_OPERATION;
    }

    if (self->has_called_activate) {
        // Should we keep this? If the user changes the is-active property when we are not configured, it's his fault.
        if (!self->is_configured) {
            g_warning("Trying to change is-active property when network is not configured!");
            return HAILO_INVALID_OPERATION;
        }
        if (old_is_active && !new_is_active) {
            self->configured_infer_model->deactivate();
        } else if (!old_is_active && new_is_active) {
            auto status = self->configured_infer_model->activate();
            CHECK_SUCCESS(status);
        } else {
            g_warning("Trying to change is-active property from %d to %d", old_is_active, new_is_active);
        }
    }

    self->props.m_is_active = new_is_active;
    return HAILO_SUCCESS;
}

static void gst_hailonet_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoNet *self = GST_HAILONET(object);
    switch (property_id) {
    case PROP_HEF_PATH:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the HEF path will not take place!");
            break;
        }
        if (nullptr != self->props.m_hef_path.get()) {
            g_free(self->props.m_hef_path.get());
        }
        self->props.m_hef_path = g_strdup(g_value_get_string(value));
        break;
    case PROP_BATCH_SIZE:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the batch size will not take place!");
            break;
        }
        self->props.m_batch_size = static_cast<guint16>(g_value_get_uint(value));
        break;
    case PROP_DEVICE_ID:
        if (0 != self->props.m_device_count.get()) {
            g_error("device-id and device-count excludes eachother. received device-id=%s, device-count=%d",
                g_value_get_string(value), self->props.m_device_count.get());
            break;
        }
        if (self->is_configured) {
            g_warning("The network was already configured so changing the device ID will not take place!");
            break;
        }
        if (nullptr != self->props.m_device_id.get()) {
            g_free(self->props.m_device_id.get());
        }
        self->props.m_device_id = g_strdup(g_value_get_string(value));
        break;
    case PROP_DEVICE_COUNT:
        if (nullptr != self->props.m_device_id.get()) {
            g_error("device-id and device-count excludes eachother. received device-id=%s, device-count=%d",
                self->props.m_device_id.get(), g_value_get_uint(value));
            break;
        }
        if (self->is_configured) {
            g_warning("The network was already configured so changing the device count will not take place!");
            break;
        }
        self->props.m_device_count = static_cast<guint16>(g_value_get_uint(value));
        break;
    case PROP_VDEVICE_GROUP_ID:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the vdevice group ID will not take place!");
            break;
        }
        if (nullptr != self->props.m_vdevice_group_id.get()) {
            g_free(self->props.m_vdevice_group_id.get());
        }
        self->props.m_vdevice_group_id = g_strdup(g_value_get_string(value));
        break;
    case PROP_IS_ACTIVE:
        (void)gst_hailonet_toggle_activation(self, self->props.m_is_active.get(), g_value_get_boolean(value));
        break;
    case PROP_PASS_THROUGH:
        self->props.m_pass_through = g_value_get_boolean(value);
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
        if (self->is_configured) {
            g_warning("The network has already been configured, the output's minimum pool size cannot be changed!");
            break;
        }
        self->props.m_outputs_min_pool_size = g_value_get_uint(value);
        break;
    case PROP_OUTPUTS_MAX_POOL_SIZE:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the outputs maximum pool size will not take place!");
            break;
        }
        self->props.m_outputs_max_pool_size = g_value_get_uint(value);
        break;
    case PROP_SCHEDULING_ALGORITHM:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the scheduling algorithm will not take place!");
            break;
        }
        if (self->props.m_is_active.was_changed() && (g_value_get_enum(value) != HAILO_SCHEDULING_ALGORITHM_NONE)) {
            g_error("scheduling-algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE in combination with 'is-active' is not supported.");
            break;
        }
        self->props.m_scheduling_algorithm = static_cast<hailo_scheduling_algorithm_t>(g_value_get_enum(value));
        break;
    case PROP_SCHEDULER_TIMEOUT_MS:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the scheduling timeout will not take place!");
            break;
        }
        self->props.m_scheduler_timeout_ms = g_value_get_uint(value);
        break;
    case PROP_SCHEDULER_THRESHOLD:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the scheduling threshold will not take place!");
            break;
        }
        self->props.m_scheduler_threshold = g_value_get_uint(value);
        break;
    case PROP_SCHEDULER_PRIORITY:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the scheduling priority will not take place!");
            break;
        }
        self->props.m_scheduler_priority = static_cast<guint8>(g_value_get_uint(value));
        break;
    case PROP_INPUT_FORMAT_TYPE:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the format type will not take place!");
            break;
        }
        self->props.m_input_format_type = static_cast<hailo_format_type_t>(g_value_get_enum(value));
        break;
    case PROP_OUTPUT_FORMAT_TYPE:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the format type will not take place!");
            break;
        }
        self->props.m_output_format_type = static_cast<hailo_format_type_t>(g_value_get_enum(value));
        break;
    case PROP_NMS_SCORE_THRESHOLD:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the score threshold will not take place!");
            break;
        }
        self->props.m_nms_score_threshold = static_cast<gfloat>(g_value_get_float(value));
        break;
    case PROP_NMS_IOU_THRESHOLD:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the IoU threshold will not take place!");
            break;
        }
        self->props.m_nms_iou_threshold = static_cast<gfloat>(g_value_get_float(value));
        break;
    case PROP_NMS_MAX_PROPOSALS_PER_CLASS:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the max proposals per class will not take place!");
            break;
        }
        self->props.m_nms_max_proposals_per_class = static_cast<guint32>(g_value_get_uint(value));
        break;
    case PROP_INPUT_FROM_META:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the input method will not take place!");
            break;
        }
        self->props.m_input_from_meta = g_value_get_boolean(value);
        break;
    case PROP_NO_TRANSFORM:
        if (self->is_configured) {
            g_warning("The network was already configured so disabling the transformation will not take place!");
        }
        self->props.m_no_transform = g_value_get_boolean(value);
        break;
    case PROP_MULTI_PROCESS_SERVICE:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the multi-process-service property will not take place!");
            break;
        }
        self->props.m_multi_process_service = g_value_get_boolean(value);
        break;
    
    // Deprecated
    case PROP_VDEVICE_KEY:
        if (self->is_configured) {
            g_warning("The network was already configured so changing the vdevice key will not take place!");
            break;
        }
        self->props.m_vdevice_key = static_cast<guint32>(g_value_get_uint(value));
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
        g_value_set_string(value, self->props.m_hef_path.get());
        break;
    case PROP_BATCH_SIZE:
        g_value_set_uint(value, self->props.m_batch_size.get());
        break;
    case PROP_DEVICE_ID:
        g_value_set_string(value, self->props.m_device_id.get());
        break;
    case PROP_DEVICE_COUNT:
        g_value_set_uint(value, self->props.m_device_count.get());
        break;
    case PROP_VDEVICE_GROUP_ID:
        g_value_set_string(value, self->props.m_vdevice_group_id.get());
        break;
    case PROP_IS_ACTIVE:
        g_value_set_boolean(value, self->props.m_is_active.get());
        break;
    case PROP_PASS_THROUGH:
        g_value_set_boolean(value, self->props.m_pass_through.get());
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
        g_value_set_uint(value, self->props.m_outputs_min_pool_size.get());
        break;
    case PROP_OUTPUTS_MAX_POOL_SIZE:
        g_value_set_uint(value, self->props.m_outputs_max_pool_size.get());
        break;
    case PROP_SCHEDULING_ALGORITHM:
        g_value_set_enum(value, self->props.m_scheduling_algorithm.get());
        break;
    case PROP_SCHEDULER_TIMEOUT_MS:
        g_value_set_uint(value, self->props.m_scheduler_timeout_ms.get());
        break;
    case PROP_SCHEDULER_THRESHOLD:
        g_value_set_uint(value, self->props.m_scheduler_threshold.get());
        break;
    case PROP_SCHEDULER_PRIORITY:
        g_value_set_uint(value, self->props.m_scheduler_priority.get());
        break;
    case PROP_INPUT_FORMAT_TYPE:
        g_value_set_enum(value, self->props.m_input_format_type.get());
        break;
    case PROP_OUTPUT_FORMAT_TYPE:
        g_value_set_enum(value, self->props.m_output_format_type.get());
        break;
    case PROP_NMS_SCORE_THRESHOLD:
        g_value_set_float(value, self->props.m_nms_score_threshold.get());
        break;
    case PROP_NMS_IOU_THRESHOLD:
        g_value_set_float(value, self->props.m_nms_iou_threshold.get());
        break;
    case PROP_NMS_MAX_PROPOSALS_PER_CLASS:
        g_value_set_uint(value, self->props.m_nms_max_proposals_per_class.get());
        break; 
    case PROP_INPUT_FROM_META:
        g_value_set_boolean(value, self->props.m_input_from_meta.get());
        break;
    case PROP_NO_TRANSFORM:
        g_value_set_boolean(value, self->props.m_no_transform.get());
        break;
    case PROP_MULTI_PROCESS_SERVICE:
        g_value_set_boolean(value, self->props.m_multi_process_service.get());
        break;
    
    // Deprecated
    case PROP_VDEVICE_KEY:
        g_value_set_uint(value, self->props.m_vdevice_key.get());
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

    g_object_class_install_property(gobject_class, PROP_MULTI_PROCESS_SERVICE,
        g_param_spec_boolean("multi-process-service", "Should run over HailoRT service", "Controls wether to run HailoRT over its service. "
            "To use this property, the service should be active and scheduling-algorithm should be set. Defaults to false.",
            HAILO_DEFAULT_MULTI_PROCESS_SERVICE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Deprecated
    g_object_class_install_property(gobject_class, PROP_VDEVICE_KEY,
        g_param_spec_uint("vdevice-key",
            "Deprecated: Indicate whether to re-use or re-create vdevice",
            "Deprecated: Use vdevice-group-id instead. Relevant only when 'device-count' is passed. If not passed, the created vdevice will be unique to this hailonet." \
            "if multiple hailonets share 'vdevice-key' and 'device-count', the created vdevice will be shared between those hailonets",
            MIN_VALID_VDEVICE_KEY, std::numeric_limits<uint32_t>::max(), MIN_VALID_VDEVICE_KEY, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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
        std::unique_lock<std::mutex> lock(self->thread_queue_mutex);
        self->thread_cv.wait(lock, [self] () {
            bool is_unlimited_pool_not_empty = (self->props.m_outputs_max_pool_size.get() == 0) && (self->buffers_in_thread_queue < MAX_OUTPUTS_POOL_SIZE);
            bool is_pool_empty = self->buffers_in_thread_queue < self->props.m_outputs_max_pool_size.get();
            return is_unlimited_pool_not_empty || is_pool_empty;
        });
        gst_queue_array_push_tail(self->thread_queue, buffer);
        self->buffers_in_thread_queue++;
    }
    self->thread_cv.notify_all();
}

static bool set_infos(GstParentBufferMeta *parent_buffer_meta, hailo_vstream_info_t &vstream_info, GstMapInfo &info)
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
    vstream_info = tensor_meta->info;
    return true;
}

static Expected<std::unordered_map<std::string, hailo_dma_buffer_t>> gst_hailonet_read_input_dma_buffers_from_meta(GstHailoNet *self, GstBuffer *buffer)
{
    std::unordered_map<std::string, hailo_dma_buffer_t> input_buffer_metas;
    gpointer state = NULL;
    GstMeta *meta;

    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_PARENT_BUFFER_META_API_TYPE))) {
        GstParentBufferMeta *parent_buffer_meta = reinterpret_cast<GstParentBufferMeta*>(meta);
        GstMapInfo info;
        hailo_vstream_info_t vstream_info;
        if (set_infos(parent_buffer_meta, vstream_info, info)) {
            CHECK_AS_EXPECTED(gst_is_dmabuf_memory(info.memory), HAILO_INTERNAL_FAILURE, "GstMemory is not a DMA buf as expected!");

            int fd = gst_fd_memory_get_fd(info.memory);
            CHECK_AS_EXPECTED(fd != -1, HAILO_INTERNAL_FAILURE, "Failed to get FD from GstMemory!");

            hailo_dma_buffer_t dma_buffer = {fd, info.size};
            input_buffer_metas[vstream_info.name] = dma_buffer;
            gst_buffer_unmap(parent_buffer_meta->buffer, &info);
        }
    }
    CHECK_AS_EXPECTED(!input_buffer_metas.empty(),HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer!");

    for (auto &input : self->infer_model->inputs()) {
        CHECK_AS_EXPECTED(input_buffer_metas.find(input.name()) != input_buffer_metas.end(),
            HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer for input: %s", input.name().c_str());
    }

    return input_buffer_metas;
}

static hailo_status gst_hailonet_fill_multiple_input_bindings_dma_buffers(GstHailoNet *self, GstBuffer *buffer)
{
    auto input_buffers = gst_hailonet_read_input_dma_buffers_from_meta(self, buffer);
    CHECK_EXPECTED_AS_STATUS(input_buffers);
    for (const auto &name : self->infer_model->get_input_names())
    {
        auto status = self->infer_bindings.input(name)->set_dma_buffer(input_buffers.value().at(name));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

static Expected<std::unordered_map<std::string, uint8_t*>> gst_hailonet_read_input_buffers_from_meta(GstHailoNet *self, GstBuffer *buffer)
{
    std::unordered_map<std::string, uint8_t*> input_buffer_metas;
    gpointer state = NULL;
    GstMeta *meta;

    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_PARENT_BUFFER_META_API_TYPE))) {
        GstParentBufferMeta *parent_buffer_meta = reinterpret_cast<GstParentBufferMeta*>(meta);
        GstMapInfo info;
        hailo_vstream_info_t vstream_info;
        if (set_infos(parent_buffer_meta, vstream_info, info)) {
            input_buffer_metas[vstream_info.name] = static_cast<uint8_t*>(info.data);
            gst_buffer_unmap(parent_buffer_meta->buffer, &info);
        }
    }
    CHECK_AS_EXPECTED(!input_buffer_metas.empty(),HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer!");

    for (auto &input : self->infer_model->inputs()) {
        CHECK_AS_EXPECTED(input_buffer_metas.find(input.name()) != input_buffer_metas.end(),
            HAILO_INTERNAL_FAILURE, "No GstHailoTensorMeta was found in buffer for input: %s", input.name().c_str());
    }

    return input_buffer_metas;
}

static hailo_status gst_hailonet_fill_multiple_input_bindings(GstHailoNet *self, GstBuffer *buffer)
{
    auto input_buffers = gst_hailonet_read_input_buffers_from_meta(self, buffer);
    CHECK_EXPECTED_AS_STATUS(input_buffers);
    for (const auto &name : self->infer_model->get_input_names()) {
        auto status = self->infer_bindings.input(name)->set_buffer(MemoryView(input_buffers.value().at(name),
            self->infer_model->input(name)->get_frame_size()));
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_push_buffer_to_input_queue(GstHailoNet *self, GstBuffer *buffer)
{
    std::unique_lock<std::mutex> lock(self->input_queue_mutex);
    gst_queue_array_push_tail(self->input_queue, buffer);

    return HAILO_SUCCESS;
}

Expected<std::unordered_map<std::string, TensorInfo>> gst_hailonet_fill_output_bindings(GstHailoNet *self)
{
    std::unordered_map<std::string, TensorInfo> tensors;
    for (auto &output : self->infer_model->outputs()) {
        GstBuffer *output_buffer = nullptr;
        GstFlowReturn flow_result = gst_buffer_pool_acquire_buffer(self->output_buffer_pools[output.name()], &output_buffer, nullptr);
        if (GST_FLOW_FLUSHING == flow_result) {
            return make_unexpected(HAILO_STREAM_ABORT);
        } else {
            CHECK_AS_EXPECTED(GST_FLOW_OK == flow_result, HAILO_INTERNAL_FAILURE, "Acquire buffer failed! flow status = %d", flow_result);
        }

        GstMapInfo buffer_info;
        gboolean result = gst_buffer_map(output_buffer, &buffer_info, GST_MAP_WRITE);
        CHECK_AS_EXPECTED(result, HAILO_INTERNAL_FAILURE, "Failed mapping buffer!");

        if (gst_hailo_should_use_dma_buffers()) {
            CHECK_AS_EXPECTED(gst_is_dmabuf_memory(buffer_info.memory), HAILO_INTERNAL_FAILURE, "GstMemory is not a DMA buf as expected!");

            int fd = gst_fd_memory_get_fd(buffer_info.memory);
            CHECK_AS_EXPECTED(fd != -1, HAILO_INTERNAL_FAILURE, "Failed to get FD from GstMemory!");

            hailo_dma_buffer_t dma_buffer = {fd, buffer_info.size};
            auto status = self->infer_bindings.output(output.name())->set_dma_buffer(dma_buffer);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            auto status = self->infer_bindings.output(output.name())->set_buffer(MemoryView(buffer_info.data, buffer_info.size));
            CHECK_SUCCESS_AS_EXPECTED(status);
        }

        tensors[output.name()] = {output_buffer, buffer_info};
    }
    return tensors;
}

static hailo_status gst_hailonet_fill_single_input_binding(GstHailoNet *self, hailo_pix_buffer_t pix_buffer)
{
    auto status = self->infer_bindings.input()->set_pix_buffer(pix_buffer);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status gst_hailonet_call_run_async(GstHailoNet *self, const std::unordered_map<std::string, TensorInfo> &tensors)
{
    auto status = self->configured_infer_model->wait_for_async_ready(WAIT_FOR_ASYNC_READY_TIMEOUT);
    CHECK_SUCCESS(status);

    {
        std::unique_lock<std::mutex> lock(self->flush_mutex);
        self->ongoing_frames++;
    }

    auto job = self->configured_infer_model->run_async(self->infer_bindings, [self, tensors] (const AsyncInferCompletionInfo &/*completion_info*/) {
        GstBuffer *buffer = nullptr;
        {
            std::unique_lock<std::mutex> lock(self->input_queue_mutex);
            buffer = static_cast<GstBuffer*>(gst_queue_array_pop_head(self->input_queue));
        }

        for (auto &output : self->infer_model->outputs()) {
            auto info = tensors.at(output.name());
            gst_buffer_unmap(info.buffer, &info.buffer_info);

            GstHailoTensorMeta *buffer_meta = GST_TENSOR_META_ADD(info.buffer);
            buffer_meta->info = self->output_vstream_infos[output.name()];

            (void)gst_buffer_add_parent_buffer_meta(buffer, info.buffer);
            gst_buffer_unref(info.buffer);
        }

        {
            std::unique_lock<std::mutex> lock(self->flush_mutex);
            self->ongoing_frames--;
        }
        self->flush_cv.notify_all();

        gst_hailonet_push_buffer_to_thread(self, buffer);
    });
    CHECK_EXPECTED_AS_STATUS(job);
    job->detach();

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
    CHECK_EXPECTED_AS_STATUS(tensors);

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
    CHECK_EXPECTED_AS_STATUS(tensors);

    status = gst_hailonet_call_run_async(self, tensors.value());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static Expected<hailo_pix_buffer_t> gst_hailonet_construct_pix_buffer(GstHailoNet *self, GstBuffer *buffer)
{
    GstVideoFrame frame;
    auto result = gst_video_frame_map(&frame, &self->input_frame_info, buffer,
        static_cast<GstMapFlags>(GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF));
    CHECK_AS_EXPECTED(result,HAILO_INTERNAL_FAILURE, "gst_video_frame_map failed!");

    hailo_pix_buffer_t pix_buffer = {};
    pix_buffer.index = 0;
    pix_buffer.number_of_planes = GST_VIDEO_INFO_N_PLANES(&frame.info);
    pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR;

    for (uint32_t plane_index = 0; plane_index < pix_buffer.number_of_planes; plane_index++) {
        pix_buffer.planes[plane_index].bytes_used = GST_VIDEO_INFO_PLANE_STRIDE(&frame.info, plane_index) * GST_VIDEO_INFO_COMP_HEIGHT(&frame.info, plane_index);
        pix_buffer.planes[plane_index].plane_size = GST_VIDEO_INFO_PLANE_STRIDE(&frame.info, plane_index) * GST_VIDEO_INFO_COMP_HEIGHT(&frame.info, plane_index);
        pix_buffer.planes[plane_index].user_ptr = GST_VIDEO_FRAME_PLANE_DATA(&frame, plane_index);
    }

    gst_video_frame_unmap(&frame);
    return pix_buffer;
}

static GstFlowReturn gst_hailonet_chain(GstPad * /*pad*/, GstObject * parent, GstBuffer * buffer)
{
    GstHailoNet *self = GST_HAILONET(parent);
    std::unique_lock<std::mutex> lock(self->infer_mutex);

    if (self->props.m_pass_through.get() || !self->props.m_is_active.get()) {
        gst_hailonet_push_buffer_to_thread(self, buffer);
        return GST_FLOW_OK;
    }

    if (self->props.m_input_from_meta.get()) {
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
    if (self->props.m_device_id.was_changed()) {
        auto expected_device_id = HailoRTCommon::to_device_id(self->props.m_device_id.get());
        CHECK_EXPECTED_AS_STATUS(expected_device_id);
        device_id = std::move(expected_device_id.release());

        vdevice_params.device_ids = &device_id;
    }
    if (self->props.m_device_count.was_changed()) {
        vdevice_params.device_count = self->props.m_device_count.get();
    }
    if (self->props.m_vdevice_group_id.was_changed()) {
        vdevice_params.group_id = self->props.m_vdevice_group_id.get();
    } else if (self->props.m_vdevice_key.was_changed()) {
        auto key_str = std::to_string(self->props.m_vdevice_key.get());
        vdevice_params.group_id = key_str.c_str();
    }
    if (self->props.m_scheduling_algorithm.was_changed()) {
        vdevice_params.scheduling_algorithm = self->props.m_scheduling_algorithm.get();
    }
    if (self->props.m_multi_process_service.was_changed()) {
        vdevice_params.multi_process_service = self->props.m_multi_process_service.get();
        CHECK(self->props.m_scheduling_algorithm.get() != HAILO_SCHEDULING_ALGORITHM_NONE, HAILO_INVALID_OPERATION,
            "To use multi-process-service please set scheduling-algorithm to a value other than 'none'");
    }

    auto vdevice = VDevice::create(vdevice_params);
    CHECK_EXPECTED_AS_STATUS(vdevice);
    self->vdevice = std::move(vdevice.release());

    auto infer_model = self->vdevice->create_infer_model(self->props.m_hef_path.get());
    CHECK_EXPECTED_AS_STATUS(infer_model);
    self->infer_model = infer_model.release();

    if(!(self->props.m_input_from_meta.get())){
        CHECK(self->infer_model->inputs().size() == 1, HAILO_INVALID_OPERATION,
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
        ERROR("Input %s has an unsupported format order! order = %d\n", input.name().c_str(), input.format().order);
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
    if (nullptr == self->vdevice) {
        auto status = gst_hailonet_init_infer_model(self);
        if (HAILO_SUCCESS != status) {
            return nullptr;
        }
    }

    // TODO (HRT-12491): check caps based on incoming metadata
    if (self->props.m_input_from_meta.get()) {
        GstCaps *new_caps = gst_caps_new_any();
        self->input_caps = new_caps;
        return gst_caps_copy(new_caps);
    }

    auto input = self->infer_model->input();
    if (!input) {
        ERROR("Getting input has failed with status = %d\n", input.status());
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

    if (!gst_video_info_from_caps(&self->input_frame_info, new_caps)) {
        ERROR("gst_video_info_from_caps failed\n");
        return nullptr;
    }

    self->input_caps = new_caps;
    return gst_caps_copy(new_caps);
}

static gboolean gst_hailonet_handle_sink_query(GstPad * pad, GstObject * parent, GstQuery * query)
{
    GstHailoNet *self = GST_HAILONET(parent);
    switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
        GstCaps *caps = gst_hailonet_get_caps(self);
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

static gboolean gst_hailonet_handle_caps_event(GstHailoNet *self, GstCaps */*caps*/)
{
    if (nullptr == self->input_caps) {
        return FALSE;
    }

    GstCaps *caps_result = gst_pad_peer_query_caps(self->srcpad, self->input_caps);
    if (gst_caps_is_empty(caps_result)) {
        return FALSE;
    }

    if (gst_caps_is_any(caps_result)) {
        gst_caps_unref(caps_result);
        return TRUE;
    }

    GstCaps *outcaps = gst_caps_fixate(caps_result);
    gboolean res = gst_pad_set_caps(self->srcpad, outcaps);
    gst_caps_unref(outcaps);
    return res;
}

static gboolean gst_hailonet_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    GstHailoNet *self = GST_HAILONET(parent);
    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS:
    {
        GstCaps *caps;
        gst_event_parse_caps(event, &caps);
        auto result = gst_hailonet_handle_caps_event(self, caps);
        gst_event_unref(event);
        return result;
    }
    case GST_EVENT_EOS:
        self->has_got_eos = true;
        return gst_pad_push_event(self->srcpad, event);
    default:
        return gst_pad_event_default(pad, parent, event);
    }
}

static GstPadProbeReturn gst_hailonet_sink_probe(GstPad */*pad*/, GstPadProbeInfo */*info*/, gpointer user_data)
{
    GstHailoNet *self = static_cast<GstHailoNet*>(user_data);
    std::unique_lock<std::mutex> lock(self->sink_probe_change_state_mutex);

    auto status = gst_hailonet_configure(self);
    if (HAILO_SUCCESS != status) {
        return GST_PAD_PROBE_DROP;
    }

    status = gst_hailonet_allocate_infer_resources(self);
    if (HAILO_SUCCESS != status) {
        return GST_PAD_PROBE_DROP;
    }

    if (HAILO_SCHEDULING_ALGORITHM_NONE != self->props.m_scheduling_algorithm.get()) {
        self->props.m_is_active = true;
        return GST_PAD_PROBE_REMOVE;
    }

    if ((1 == hailonet_count) && (!self->props.m_is_active.was_changed())) {
        self->props.m_is_active = true;
    }

    if (self->props.m_is_active.get()) {
        status = self->configured_infer_model->activate();
        if (HAILO_SUCCESS != status) {
            return GST_PAD_PROBE_DROP;
        }
    }

    self->has_called_activate = true;
    return GST_PAD_PROBE_REMOVE;
}

static void gst_hailonet_flush_callback(GstHailoNet *self, gpointer /*data*/)
{
    std::unique_lock<std::mutex> lock(self->flush_mutex);
    self->flush_cv.wait(lock, [self] () {
        return 0 == self->ongoing_frames;
    });
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

    self->input_caps = nullptr;
    self->input_queue = nullptr;
    self->thread_queue = nullptr;
    self->is_thread_running = false;
    self->has_got_eos = false;
    self->buffers_in_thread_queue = 0;
    self->props = HailoNetProperties();
    self->vdevice = nullptr;
    self->is_configured = false;
    self->has_called_activate = false;
    self->ongoing_frames = 0;

    gchar *parent_name = gst_object_get_name(GST_OBJECT(self));
    gchar *name = g_strconcat(parent_name, ":hailo_allocator", NULL);
    g_free(parent_name);

    if (gst_hailo_should_use_dma_buffers()) {
        self->dma_allocator = gst_dmabuf_allocator_new();
    } else {
        self->allocator = GST_HAILO_ALLOCATOR(g_object_new(GST_TYPE_HAILO_ALLOCATOR, "name", name, NULL));
        gst_object_ref_sink(self->allocator);
        g_free(name);
    }

    g_signal_connect(self, "flush", G_CALLBACK(gst_hailonet_flush_callback), nullptr);

    hailonet_count++;
}
