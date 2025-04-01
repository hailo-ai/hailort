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
#ifndef _GST_HAILORECV_HPP_
#define _GST_HAILORECV_HPP_

#include "common.hpp"
#include "hailo_output_info.hpp"

#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

#include <vector>

G_BEGIN_DECLS

#define GST_TYPE_HAILO_BUFFER_POOL (gst_hailo_buffer_pool_get_type())
#define GST_HAILO_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HAILO_BUFFER_POOL, GstHailoBufferPool))
#define GST_HAILO_BUFFER_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HAILO_BUFFER_POOL, GstHailoBufferPoolClass))
#define GST_IS_HAILO_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HAILO_BUFFER_POOL))
#define GST_IS_HAILO_BUFFER_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HAILO_BUFFER_POOL))

struct GstHailoBufferPool
{
    GstBufferPool parent;
    // This is a copy of the vstream name because vstreams can be freed before the pool has finished freeing all the buffers
    char vstream_name[HAILO_MAX_STREAM_NAME_SIZE];
    const char *element_name;
    std::atomic_uint buffers_acquired;
};

struct GstHailoBufferPoolClass
{
    GstBufferPoolClass parent;
    GstFlowReturn (*parent_acquire_callback)(GstBufferPool *, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
    void (*parent_release_callback)(GstBufferPool *pool, GstBuffer *buffer);
};

GType gst_hailo_buffer_pool_get_type(void);

#define GST_TYPE_HAILORECV (gst_hailorecv_get_type())
#define GST_HAILORECV(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HAILORECV,GstHailoRecv))
#define GST_HAILORECV_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HAILORECV,GstHailoRecvClass))
#define GST_IS_HAILORECV(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HAILORECV))
#define GST_IS_HAILORECV_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HAILORECV))

class HailoRecvImpl;
struct GstHailoRecv
{
    GstVideoFilter parent;
    std::unique_ptr<HailoRecvImpl> impl;
};

struct GstHailoRecvClass
{
    GstVideoFilterClass parent;
};

struct HailoRecvProperties final
{
public:
    HailoRecvProperties() : m_debug(false), m_outputs_min_pool_size(DEFAULT_OUTPUTS_MIN_POOL_SIZE), m_outputs_max_pool_size(DEFAULT_OUTPUTS_MAX_POOL_SIZE)
    {}

    HailoElemProperty<gboolean> m_debug;
    HailoElemProperty<guint> m_outputs_min_pool_size;
    HailoElemProperty<guint> m_outputs_max_pool_size;
};

class HailoRecvImpl final {
public:
    static Expected<std::unique_ptr<HailoRecvImpl>> create(GstHailoRecv *element);
    HailoRecvImpl(GstHailoRecv *element);
    void set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
    void get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
    GstFlowReturn handle_frame(GstVideoFilter *filter, GstVideoFrame *frame);
    hailo_status set_output_vstreams(std::vector<OutputVStream> &&output_vstreams, uint32_t batch_size);
    hailo_status clear_vstreams();
    hailo_status abort_vstreams();

private:
    hailo_status read_from_vstreams(bool should_print_latency);
    hailo_status write_tensors_to_metadata(GstVideoFrame *frame, bool should_print_latency);

    GstHailoRecv *m_element;
    HailoRecvProperties m_props;
    std::vector<OutputVStream> m_output_vstreams;
    std::vector<HailoOutputInfo> m_output_infos;
};

GType gst_hailorecv_get_type(void);

G_END_DECLS

#endif /* _GST_HAILORECV_HPP_ */
