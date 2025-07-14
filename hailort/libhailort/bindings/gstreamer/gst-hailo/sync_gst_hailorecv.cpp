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
#include "sync_gst_hailorecv.hpp"
#include "sync_gsthailonet.hpp"
#include "common.hpp"
#include "network_group_handle.hpp"
#include "metadata/hailo_buffer_flag_meta.hpp"
#include "tensor_meta.hpp"
#include "hailo_events/hailo_events.hpp"

#include <iostream>
#include <numeric>
#include <algorithm>

GST_DEBUG_CATEGORY_STATIC(gst_hailorecv_debug_category);
#define GST_CAT_DEFAULT gst_hailorecv_debug_category

static void gst_hailorecv_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailorecv_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_hailorecv_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame);
static GstStateChangeReturn gst_hailorecv_change_state(GstElement *element, GstStateChange transition);
static GstFlowReturn gst_hailorecv_buffer_pool_acquire_callback(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
static void gst_hailorecv_buffer_pool_release_callback(GstBufferPool *pool, GstBuffer *buffer);

G_DEFINE_TYPE(GstHailoBufferPool, gst_hailo_buffer_pool, GST_TYPE_BUFFER_POOL);

static void gst_hailo_buffer_pool_class_init(GstHailoBufferPoolClass *klass)
{
    GstBufferPoolClass *buffer_pool_class = GST_BUFFER_POOL_CLASS(klass);
    klass->parent_acquire_callback = buffer_pool_class->acquire_buffer;
    klass->parent_release_callback = buffer_pool_class->release_buffer;
    buffer_pool_class->acquire_buffer = gst_hailorecv_buffer_pool_acquire_callback;
    buffer_pool_class->release_buffer = gst_hailorecv_buffer_pool_release_callback;
}

static void gst_hailo_buffer_pool_init(GstHailoBufferPool *self)
{
    self->element_name = nullptr;
    self->buffers_acquired = 0;
}

enum
{
    PROP_0,
    PROP_DEBUG,
    PROP_OUTPUTS_MIN_POOL_SIZE,
    PROP_OUTPUTS_MAX_POOL_SIZE
};

G_DEFINE_TYPE(GstHailoRecv, gst_hailorecv, GST_TYPE_VIDEO_FILTER);

static void gst_hailorecv_class_init(GstHailoRecvClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);

    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
            gst_caps_from_string(HAILO_VIDEO_CAPS)));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
        gst_caps_from_string(HAILO_VIDEO_CAPS)));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
        "hailorecv element", "Hailo/Filter/Video", "Receive data from HailoRT", PLUGIN_AUTHOR);
    
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailorecv_change_state);

    gobject_class->set_property = gst_hailorecv_set_property;
    gobject_class->get_property = gst_hailorecv_get_property;
    g_object_class_install_property(gobject_class, PROP_DEBUG,
        g_param_spec_boolean("debug", "debug", "debug", false,
            (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUTS_MIN_POOL_SIZE,
        g_param_spec_uint("outputs-min-pool-size", "Outputs Minimun Pool Size", "The minimum amount of buffers to allocate for each output layer",
            0, std::numeric_limits<uint32_t>::max(), 1, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUTS_MAX_POOL_SIZE,
        g_param_spec_uint("outputs-max-pool-size", "Outputs Maximum Pool Size",
            "The maximum amount of buffers to allocate for each output layer or 0 for unlimited", 0, std::numeric_limits<uint32_t>::max(), 1,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    video_filter_class->transform_frame_ip = GST_DEBUG_FUNCPTR(gst_hailorecv_transform_frame_ip);
}

Expected<std::unique_ptr<HailoRecvImpl>> HailoRecvImpl::create(GstHailoRecv *element)
{
    if (nullptr == element) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    auto ptr = make_unique_nothrow<HailoRecvImpl>(element);
    if (nullptr == ptr) {
        return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
    }

    return ptr;
}

HailoRecvImpl::HailoRecvImpl(GstHailoRecv *element) : m_element(element), m_props()
{
    GST_DEBUG_CATEGORY_INIT(gst_hailorecv_debug_category, "hailorecv", 0, "debug category for hailorecv element");
}

void HailoRecvImpl::set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GST_DEBUG_OBJECT(m_element, "set_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr)) {
        g_error("set_property got null parameter!");
        return;
    }

    switch (property_id) {
    case PROP_DEBUG:
        m_props.m_debug = g_value_get_boolean(value);
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
        m_props.m_outputs_min_pool_size = g_value_get_uint(value);
        break;
    case PROP_OUTPUTS_MAX_POOL_SIZE:
        m_props.m_outputs_max_pool_size = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void HailoRecvImpl::get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GST_DEBUG_OBJECT(m_element, "get_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr)) {
        g_error("get_property got null parameter!");
        return;
    }

    switch (property_id) {
    case PROP_DEBUG:
        g_value_set_boolean(value, m_props.m_debug.get());
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
        g_value_set_uint(value, m_props.m_outputs_min_pool_size.get());
        break;
    case PROP_OUTPUTS_MAX_POOL_SIZE:
        g_value_set_uint(value, m_props.m_outputs_max_pool_size.get());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

GstFlowReturn HailoRecvImpl::handle_frame(GstVideoFilter */*filter*/, GstVideoFrame *frame)
{
    assert(nullptr != frame);

    gpointer state = nullptr;
    GstHailoBufferFlagMeta *meta = GST_HAILO_BUFFER_FLAG_META_ITERATE(frame->buffer, &state);

    // Frames without metadata should get a metadata with the output tensors
    if  (nullptr != meta) {
        switch (meta->flag) {
        case BUFFER_FLAG_FLUSH:
        {
            hailo_status status = GST_SYNC_HAILONET(GST_ELEMENT_PARENT(m_element))->impl->signal_was_flushed_event();
            GST_CHECK(HAILO_SUCCESS == status, GST_FLOW_ERROR, m_element, RESOURCE, "Signalling was flushed event has failed, status = %d", status);
            return GST_BASE_TRANSFORM_FLOW_DROPPED;
        }
        case BUFFER_FLAG_SKIP:
            return GST_FLOW_OK;
        case BUFFER_FLAG_NONE:
        default:
            g_error("Unknown metadata type = %d", meta->flag);
            break;
        }
    }

    if (!GST_SYNC_HAILONET(GST_ELEMENT_PARENT(m_element))->impl->is_active()) {
        return GST_FLOW_OK;
    }

    hailo_status status = read_from_vstreams(m_props.m_debug.get());
    if (HAILO_SUCCESS != status) {
        return GST_FLOW_ERROR;
    }

    status = write_tensors_to_metadata(frame, m_props.m_debug.get());
    if (HAILO_SUCCESS != status) {
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

hailo_status HailoRecvImpl::read_from_vstreams(bool should_print_latency)
{
    std::chrono::time_point<std::chrono::system_clock> overall_start_time = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point start_time;

    for (auto &output_info : m_output_infos) {
        if (should_print_latency) {
            start_time = std::chrono::system_clock::now();
        }

        GstMapInfo buffer_info;
        auto buffer = output_info.acquire_buffer();
        GST_CHECK_EXPECTED_AS_STATUS(buffer, m_element, RESOURCE, "Failed to acquire buffer!");

        gboolean result = gst_buffer_map(*buffer, &buffer_info, GST_MAP_WRITE);
        GST_CHECK(result, HAILO_INTERNAL_FAILURE, m_element, RESOURCE, "Failed mapping buffer!");

        auto status = output_info.vstream().read(MemoryView(buffer_info.data, buffer_info.size));
        if (should_print_latency) {
            std::chrono::duration<double, std::milli> latency = std::chrono::system_clock::now() - start_time;
            GST_DEBUG("%s latency: %f milliseconds", output_info.vstream().name().c_str(), latency.count());
        }
        gst_buffer_unmap(*buffer, &buffer_info);
        if (HAILO_STREAM_ABORT == status) {
            return status;
        }
        GST_CHECK_SUCCESS(status, m_element, STREAM, "Reading from vstream failed, status = %d", status);
    }

    if (should_print_latency) {
        std::chrono::duration<double, std::milli>  latency = std::chrono::system_clock::now() - overall_start_time;
        GST_DEBUG("hailorecv read latency: %f milliseconds", latency.count());
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRecvImpl::write_tensors_to_metadata(GstVideoFrame *frame, bool should_print_latency)
{
    std::chrono::time_point<std::chrono::system_clock> start_time = std::chrono::system_clock::now();
    for (auto &output_info : m_output_infos) {
        GstHailoTensorMeta *buffer_meta = GST_TENSOR_META_ADD(output_info.last_acquired_buffer());
        buffer_meta->info = tensor_metadata_from_vstream_info(output_info.vstream_info());

        (void)gst_buffer_add_parent_buffer_meta(frame->buffer, output_info.last_acquired_buffer());
        output_info.unref_last_acquired_buffer();
    }

    if (should_print_latency) {
        std::chrono::duration<double, std::milli> latency = std::chrono::system_clock::now() - start_time;
        GST_DEBUG("hailorecv metadata latency: %f milliseconds", latency.count());
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRecvImpl::set_output_vstreams(std::vector<OutputVStream> &&output_vstreams, uint32_t batch_size)
{
    GST_CHECK((0 == m_props.m_outputs_max_pool_size.get()) || (m_props.m_outputs_min_pool_size.get() <= m_props.m_outputs_max_pool_size.get()),
        HAILO_INVALID_ARGUMENT, m_element, RESOURCE, "Minimum pool size (=%d) is bigger than maximum (=%d)!", m_props.m_outputs_min_pool_size.get(),
        m_props.m_outputs_max_pool_size.get());
    
    if ((0 != m_props.m_outputs_max_pool_size.get()) && (m_props.m_outputs_max_pool_size.get() < batch_size)) {
        g_warning("outputs-max-pool-size is smaller than the batch size! Overall performance might be affected!");
    }

    m_output_vstreams = std::move(output_vstreams);

    for (auto &out_vstream : m_output_vstreams) {
        GstHailoBufferPool *hailo_pool = GST_HAILO_BUFFER_POOL(g_object_new(GST_TYPE_HAILO_BUFFER_POOL, NULL));
        gst_object_ref_sink(hailo_pool);
        strncpy(hailo_pool->vstream_name, out_vstream.name().c_str(), out_vstream.name().length() + 1);
        hailo_pool->element_name = GST_ELEMENT_NAME(GST_ELEMENT_PARENT(m_element));

        GstBufferPool *pool = GST_BUFFER_POOL(hailo_pool);

        GstStructure *config = gst_buffer_pool_get_config(pool);

        gst_buffer_pool_config_set_params(config, nullptr, static_cast<guint>(out_vstream.get_frame_size()), m_props.m_outputs_min_pool_size.get(),
            m_props.m_outputs_max_pool_size.get());

        gboolean result = gst_buffer_pool_set_config(pool, config);
        GST_CHECK(result, HAILO_INTERNAL_FAILURE, m_element, RESOURCE, "Could not set config for vstream %s buffer pool", out_vstream.name().c_str());

        result = gst_buffer_pool_set_active(pool, TRUE);
        GST_CHECK(result, HAILO_INTERNAL_FAILURE, m_element, RESOURCE, "Could not set buffer pool active for vstream %s", out_vstream.name().c_str());

        m_output_infos.emplace_back(out_vstream, pool);
    }

    return HAILO_SUCCESS;
}

hailo_status HailoRecvImpl::clear_vstreams()
{
    return OutputVStream::clear(m_output_vstreams);
}

hailo_status HailoRecvImpl::abort_vstreams()
{
    for (auto& output_vstream : m_output_vstreams) {
        auto status = output_vstream.abort();
        GST_CHECK_SUCCESS(status, m_element, STREAM, "Failed aborting output vstream %s, status = %d", output_vstream.name().c_str(), status);
    }
    return HAILO_SUCCESS;
}

static void gst_hailorecv_init(GstHailoRecv *self)
{
    auto hailorecv_impl = HailoRecvImpl::create(self);
    if (!hailorecv_impl) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Creating hailorecv implementation has failed! status = %d", hailorecv_impl.status()), (NULL));
        return;
    }

    self->impl = hailorecv_impl.release();
}

static void gst_hailorecv_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GST_HAILORECV(object)->impl->set_property(object, property_id, value, pspec);
}

static void gst_hailorecv_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GST_HAILORECV(object)->impl->get_property(object, property_id, value, pspec);
}

static GstFlowReturn gst_hailorecv_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame)
{
    GST_DEBUG_OBJECT(filter, "transform_frame_ip");
    return GST_HAILORECV(filter)->impl->handle_frame(filter, frame);
}

static GstStateChangeReturn gst_hailorecv_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_hailorecv_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        return ret;
    }

    if (GST_STATE_CHANGE_READY_TO_NULL == transition) {
        auto status = GST_HAILORECV(element)->impl->abort_vstreams();
        GST_CHECK(HAILO_SUCCESS == status, GST_STATE_CHANGE_FAILURE, element, STREAM, "Aborting output vstreams failed, status = %d\n", status);
        // Cleanup all of hailorecv memory
        GST_HAILORECV(element)->impl.reset();
    }

    return ret;
}

static GstFlowReturn gst_hailorecv_buffer_pool_acquire_callback(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params)
{
    GstHailoBufferPool *hailo_pool = GST_HAILO_BUFFER_POOL(pool);

    GstFlowReturn status = GST_HAILO_BUFFER_POOL_CLASS(GST_BUFFER_POOL_GET_CLASS(pool))->parent_acquire_callback(pool, buffer, params);
    if (GST_FLOW_OK == status) {
        ++hailo_pool->buffers_acquired;

        GstStructure *pool_config = gst_buffer_pool_get_config(pool);
        guint max_buffers = 0;
        gboolean result = gst_buffer_pool_config_get_params(pool_config, NULL, NULL, NULL, &max_buffers);
        gst_structure_free(pool_config);
        if (!result) {
            g_error("Failed getting config params from buffer pool!");
            return GST_FLOW_ERROR;
        }

        if (hailo_pool->buffers_acquired.load() == max_buffers) {
            GST_INFO("Buffer pool of vstream %s in element %s is overrun!", hailo_pool->vstream_name, hailo_pool->element_name);
        }
    }
    return status;
}

static void gst_hailorecv_buffer_pool_release_callback(GstBufferPool *pool, GstBuffer *buffer)
{
    GstHailoBufferPool *hailo_pool = GST_HAILO_BUFFER_POOL(pool);

    GST_HAILO_BUFFER_POOL_CLASS(GST_BUFFER_POOL_GET_CLASS(pool))->parent_release_callback(pool, buffer);
    if (hailo_pool->buffers_acquired > 0) {
        --hailo_pool->buffers_acquired;
        if (hailo_pool->buffers_acquired.load() == 0) {
            GST_INFO("Buffer pool of vstream %s in element %s is underrun!", hailo_pool->vstream_name, hailo_pool->element_name);
        }
    }
}