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
#ifndef _GST_HAILOSEND_HPP_
#define _GST_HAILOSEND_HPP_

#include "common.hpp"
#include "network_group_handle.hpp"
#include "sync_gsthailonet.hpp"

#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

#include <vector>

G_BEGIN_DECLS

#define GST_TYPE_HAILOSEND (gst_hailosend_get_type())
#define GST_HAILOSEND(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HAILOSEND,GstHailoSend))
#define GST_HAILOSEND_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HAILOSEND,GstHailoSendClass))
#define GST_IS_HAILOSEND(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HAILOSEND))
#define GST_IS_HAILOSEND_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HAILOSEND))

class HailoSendImpl;
struct GstHailoSend
{
    GstVideoFilter parent;
    std::unique_ptr<HailoSendImpl> impl;
};

struct GstHailoSendClass
{
    GstVideoFilterClass parent;
};

struct HailoSendProperties final
{
public:
    HailoSendProperties() : m_debug(false)
    {}

    HailoElemProperty<gboolean> m_debug;
};

class HailoSendImpl final
{
public:
    static Expected<std::unique_ptr<HailoSendImpl>> create(GstHailoSend *element);
    HailoSendImpl(GstHailoSend *element);

    void set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
    void get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
    GstFlowReturn handle_frame(GstVideoFilter *filter, GstVideoFrame *frame);
    GstCaps *get_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter);
    void set_input_vstream_infos(std::vector<hailo_vstream_info_t> &&input_vstream_infos);
    void set_input_vstreams(std::vector<InputVStream> &&input_vstreams);
    hailo_status clear_vstreams();
    hailo_status abort_vstreams();

    void set_batch_size(uint32_t batch_size)
    {
        m_batch_size = batch_size;
    }

    uint32_t batch_size()
    {
        return m_batch_size;
    }

    GstClockTime last_frame_pts()
    {
        return m_last_frame_pts;
    }

private:
    hailo_status write_to_vstreams(const hailo_pix_buffer_t &pix_buffer);
    
    GstHailoSend *m_element;
    GstSyncHailoNet *m_sync_hailonet;
    HailoSendProperties m_props;
    std::vector<hailo_vstream_info_t> m_input_vstream_infos;
    uint32_t m_batch_size;
    std::vector<InputVStream> m_input_vstreams;
    GstClockTime m_last_frame_pts;
};

GType gst_hailosend_get_type(void);

G_END_DECLS

#endif /* _GST_HAILOSEND_HPP_ */
