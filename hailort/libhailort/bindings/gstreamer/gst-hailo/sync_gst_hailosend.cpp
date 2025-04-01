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
#include "sync_gst_hailosend.hpp"
#include "sync_gsthailonet.hpp"
#include "metadata/hailo_buffer_flag_meta.hpp"

#include <chrono>
#include <iostream>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

GST_DEBUG_CATEGORY_STATIC(gst_hailosend_debug_category);
#define GST_CAT_DEFAULT gst_hailosend_debug_category

static void gst_hailosend_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailosend_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstCaps *gst_hailosend_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static GstFlowReturn gst_hailosend_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame);
static gboolean gst_hailosend_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query);
static GstStateChangeReturn gst_hailosend_change_state(GstElement *element, GstStateChange transition);

enum
{
    PROP_0,
    PROP_DEBUG
};

G_DEFINE_TYPE(GstHailoSend, gst_hailosend, GST_TYPE_VIDEO_FILTER);

static void gst_hailosend_class_init(GstHailoSendClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);

    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string(HAILO_VIDEO_CAPS)));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string(HAILO_VIDEO_CAPS)));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
        "hailosend element", "Hailo/Filter/Video", "Send RGB/RGBA/GRAY8/YUY2/NV12/NV21/I420 video to HailoRT", PLUGIN_AUTHOR);
    
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailosend_change_state);

    gobject_class->set_property = gst_hailosend_set_property;
    gobject_class->get_property = gst_hailosend_get_property;
    g_object_class_install_property (gobject_class, PROP_DEBUG,
        g_param_spec_boolean ("debug", "debug", "debug", false,
            (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_hailosend_transform_caps);
    base_transform_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_hailosend_propose_allocation);
    video_filter_class->transform_frame_ip = GST_DEBUG_FUNCPTR(gst_hailosend_transform_frame_ip);
}

Expected<std::unique_ptr<HailoSendImpl>> HailoSendImpl::create(GstHailoSend *element)
{
    if (nullptr == element) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    auto ptr = make_unique_nothrow<HailoSendImpl>(element);
    if (nullptr == ptr) {
        return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
    }

    return ptr;
}

HailoSendImpl::HailoSendImpl(GstHailoSend *element) : m_element(element), m_sync_hailonet(nullptr), m_props(),
    m_batch_size(HAILO_DEFAULT_BATCH_SIZE), m_last_frame_pts(0)
{
    GST_DEBUG_CATEGORY_INIT(gst_hailosend_debug_category, "hailosend", 0, "debug category for hailosend element");
}

void HailoSendImpl::set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
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
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void HailoSendImpl::get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
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
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

GstFlowReturn HailoSendImpl::handle_frame(GstVideoFilter */*filter*/, GstVideoFrame *frame)
{
    assert(nullptr != frame);
    m_last_frame_pts = GST_BUFFER_TIMESTAMP(frame->buffer);

    if (!GST_SYNC_HAILONET(GST_ELEMENT_PARENT(m_element))->impl->is_active()) {
        GstHailoBufferFlagMeta *meta = GST_HAILO_BUFFER_FLAG_META_ADD(frame->buffer);
        meta->flag = BUFFER_FLAG_SKIP;
        return GST_FLOW_OK;
    }

    hailo_pix_buffer_t pix_buffer = {};
    pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR;
    pix_buffer.index = 0;
    pix_buffer.number_of_planes = GST_VIDEO_INFO_N_PLANES(&frame->info);
    for (uint32_t plane_index = 0; plane_index < pix_buffer.number_of_planes; plane_index++) {
        pix_buffer.planes[plane_index].bytes_used = GST_VIDEO_INFO_PLANE_STRIDE(&frame->info, plane_index) * GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, plane_index);
        pix_buffer.planes[plane_index].plane_size = GST_VIDEO_INFO_PLANE_STRIDE(&frame->info, plane_index) * GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, plane_index);
        pix_buffer.planes[plane_index].user_ptr = GST_VIDEO_FRAME_PLANE_DATA(frame, plane_index);
    }

    hailo_status status = HAILO_UNINITIALIZED;

    if (m_props.m_debug.get()) {
        std::chrono::duration<double, std::milli> latency;
        std::chrono::time_point<std::chrono::system_clock> start_time;
        start_time = std::chrono::system_clock::now();
        status = write_to_vstreams(pix_buffer);
        latency = std::chrono::system_clock::now() - start_time;
        GST_DEBUG("hailosend latency: %f milliseconds", latency.count());
    } else {
        status = write_to_vstreams(pix_buffer);
    }

    if (HAILO_SUCCESS != status) {
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

hailo_status HailoSendImpl::write_to_vstreams(const hailo_pix_buffer_t &pix_buffer)
{
    for (auto &in_vstream : m_input_vstreams) {
        auto status = in_vstream.write(pix_buffer);
        if (HAILO_STREAM_ABORT == status) {
            return status;
        }
        GST_CHECK_SUCCESS(status, m_element, STREAM, "Failed writing to input vstream %s, status = %d", in_vstream.name().c_str(), status);
    }
    return HAILO_SUCCESS;
}

uint32_t get_height_by_order(const hailo_vstream_info_t &input_vstream_info)
{
    auto original_height = input_vstream_info.shape.height;
    switch (input_vstream_info.format.order) {
    case HAILO_FORMAT_ORDER_NV12:
    case HAILO_FORMAT_ORDER_NV21:
        return original_height * 2;
    default:
        break;
    }
    return original_height;
}

GstCaps *HailoSendImpl::get_caps(GstBaseTransform */*trans*/, GstPadDirection /*direction*/, GstCaps *caps, GstCaps */*filter*/)
{
    GST_DEBUG_OBJECT(m_element, "transform_caps");

    if (0 == m_input_vstream_infos.size()) {
        // Init here because it is guaranteed that we have a parent element
        m_sync_hailonet = GST_SYNC_HAILONET(GST_ELEMENT_PARENT(m_element));

        hailo_status status = m_sync_hailonet->impl->set_hef();
        if (HAILO_SUCCESS != status) {
            return NULL;
        }
    }
    
    const gchar *format = nullptr;
    switch (m_input_vstream_infos[0].format.order) {
    case HAILO_FORMAT_ORDER_RGB4:
    case HAILO_FORMAT_ORDER_NHWC:
        if (m_input_vstream_infos[0].shape.features == RGBA_FEATURES_SIZE) {
            format = "RGBA";
            break;
        }
        else if (m_input_vstream_infos[0].shape.features == GRAY8_FEATURES_SIZE)
        {
            format = "GRAY8";
            break;
        }
        /* Fallthrough */
    case HAILO_FORMAT_ORDER_NHCW:
    case HAILO_FORMAT_ORDER_FCR:
    case HAILO_FORMAT_ORDER_F8CR:
        if (m_input_vstream_infos[0].shape.features == GRAY8_FEATURES_SIZE)
        {
            format = "GRAY8";
            break;
        }
        else
        {
            format = "RGB";
            GST_CHECK(RGB_FEATURES_SIZE == m_input_vstream_infos[0].shape.features, NULL, m_element, STREAM,
                "Features of input vstream %s is not %d for RGB format! (features=%d)", m_input_vstream_infos[0].name, RGB_FEATURES_SIZE,
                m_input_vstream_infos[0].shape.features);
            break;
        }
    case HAILO_FORMAT_ORDER_YUY2:
        format = "YUY2";
        GST_CHECK(YUY2_FEATURES_SIZE == m_input_vstream_infos[0].shape.features, NULL, m_element, STREAM,
            "Features of input vstream %s is not %d for YUY2 format! (features=%d)", m_input_vstream_infos[0].name, YUY2_FEATURES_SIZE,
            m_input_vstream_infos[0].shape.features);
        break;
    case HAILO_FORMAT_ORDER_NV12:
        format = "NV12";
        GST_CHECK(NV12_FEATURES_SIZE == m_input_vstream_infos[0].shape.features, NULL, m_element, STREAM,
            "Features of input vstream %s is not %d for NV12 format! (features=%d)", m_input_vstream_infos[0].name, NV12_FEATURES_SIZE,
            m_input_vstream_infos[0].shape.features);
        break;
    case HAILO_FORMAT_ORDER_NV21:
        format = "NV21";
        GST_CHECK(NV21_FEATURES_SIZE == m_input_vstream_infos[0].shape.features, NULL, m_element, STREAM,
            "Features of input vstream %s is not %d for NV21 format! (features=%d)", m_input_vstream_infos[0].name, NV21_FEATURES_SIZE,
            m_input_vstream_infos[0].shape.features);
        break;
    case HAILO_FORMAT_ORDER_I420:
        format = "I420";
        GST_CHECK(I420_FEATURES_SIZE == m_input_vstream_infos[0].shape.features, NULL, m_element, STREAM,
            "Features of input vstream %s is not %d for I420 format! (features=%d)", m_input_vstream_infos[0].name, I420_FEATURES_SIZE,
            m_input_vstream_infos[0].shape.features);
        break;
    default:
        GST_ELEMENT_ERROR(m_element, RESOURCE, FAILED,
            ("Input VStream %s has an unsupported format order! order = %d", m_input_vstream_infos[0].name, m_input_vstream_infos[0].format.order), (NULL));
        return NULL;
    }
    /* filter against set allowed caps on the pad */
    GstCaps *new_caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, format,
                                            "width", G_TYPE_INT, m_input_vstream_infos[0].shape.width,
                                            "height", G_TYPE_INT, get_height_by_order(m_input_vstream_infos[0]),
                                            NULL);
    auto result = gst_caps_intersect(caps, new_caps);
    gst_caps_unref(new_caps);
    return result;
}

void HailoSendImpl::set_input_vstream_infos(std::vector<hailo_vstream_info_t> &&input_vstream_infos)
{
    m_input_vstream_infos = std::move(input_vstream_infos);
}

void HailoSendImpl::set_input_vstreams(std::vector<InputVStream> &&input_vstreams)
{
    m_input_vstreams = std::move(input_vstreams);
}

hailo_status HailoSendImpl::clear_vstreams()
{
    return InputVStream::clear(m_input_vstreams);
}

hailo_status HailoSendImpl::abort_vstreams()
{
    for (auto& input_vstream : m_input_vstreams) {
        auto status = input_vstream.abort();
        GST_CHECK_SUCCESS(status, m_element, STREAM, "Failed aborting input vstream %s, status = %d", input_vstream.name().c_str(), status);
    }
    return HAILO_SUCCESS;
}

static void gst_hailosend_init(GstHailoSend *self)
{
    auto hailosend_impl = HailoSendImpl::create(self);
    if (!hailosend_impl) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Creating hailosend implementation has failed! status = %d", hailosend_impl.status()), (NULL));
        return;
    }

    self->impl = hailosend_impl.release();
}

void gst_hailosend_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GST_HAILOSEND(object)->impl->set_property(object, property_id, value, pspec);
}

void gst_hailosend_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GST_HAILOSEND(object)->impl->get_property(object, property_id, value, pspec);
}

static GstFlowReturn gst_hailosend_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame)
{
    GST_DEBUG_OBJECT(filter, "transform_frame_ip");
    return GST_HAILOSEND(filter)->impl->handle_frame(filter, frame);
}

static GstCaps *gst_hailosend_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
    return GST_HAILOSEND(trans)->impl->get_caps(trans, direction, caps, filter);
}

static gboolean gst_hailosend_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query)
{
    if (GST_HAILOSEND(trans)->impl->batch_size() > 1) {
        return FALSE;
    }

    return GST_BASE_TRANSFORM_CLASS(gst_hailosend_parent_class)->propose_allocation(trans, decide_query, query);
}

static GstStateChangeReturn gst_hailosend_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_hailosend_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        return ret;
    }

    if (GST_STATE_CHANGE_READY_TO_NULL == transition) {
        auto status = GST_HAILOSEND(element)->impl->abort_vstreams();
        GST_CHECK(HAILO_SUCCESS == status, GST_STATE_CHANGE_FAILURE, element, STREAM, "Aborting input vstreams failed, status = %d\n", status);
        // Cleanup all of hailosend memory
        GST_HAILOSEND(element)->impl.reset();
    }

    return ret;
}