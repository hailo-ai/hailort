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
#ifndef __HAILO_BUFFER_FLAG_META_HPP__
#define __HAILO_BUFFER_FLAG_META_HPP__

#include "include/hailo_gst.h"

#define HAILO_BUFFER_FLAG_META_API_NAME "GstHailoBufferFlagMetaAPI"
#define HAILO_BUFFER_FLAG_META_IMPL_NAME "GstHailoBufferFlagMeta"
#define HAILO_BUFFER_FLAG_META_TAG "hailo_buffer_flag_meta"

G_BEGIN_DECLS

enum BufferFlag {
    BUFFER_FLAG_NONE,
    BUFFER_FLAG_SKIP,
    BUFFER_FLAG_FLUSH
};

struct GstHailoBufferFlagMeta {
    GstMeta meta;
    BufferFlag flag;
};

const GstMetaInfo *gst_hailo_buffer_flag_meta_get_info(void);

GType gst_hailo_buffer_flag_meta_api_get_type(void);

#define GST_HAILO_BUFFER_FLAG_META_API_TYPE (gst_hailo_buffer_flag_meta_api_get_type())
#define GST_HAILO_BUFFER_FLAG_META_INFO (gst_hailo_buffer_flag_meta_get_info())
#define GST_HAILO_BUFFER_FLAG_META_GET(buf) ((GstHailoBufferFlagMeta *)gst_buffer_get_meta(buf, gst_hailo_buffer_flag_meta_api_get_type()))
#define GST_HAILO_BUFFER_FLAG_META_ITERATE(buf, state)                                                                        \
    ((GstHailoBufferFlagMeta *)gst_buffer_iterate_meta_filtered(buf, state, gst_hailo_buffer_flag_meta_api_get_type()))
#define GST_HAILO_BUFFER_FLAG_META_ADD(buf)                                                                                   \
    ((GstHailoBufferFlagMeta *)gst_buffer_add_meta(buf, gst_hailo_buffer_flag_meta_get_info(), NULL))
#define GST_HAILO_BUFFER_FLAG_META_COUNT(buf) (gst_buffer_get_n_meta(buf, gst_hailo_buffer_flag_meta_api_get_type()))

G_END_DECLS

#endif /* __HAILO_BUFFER_FLAG_META_HPP__ */
