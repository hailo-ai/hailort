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
#include "hailo_buffer_flag_meta.hpp"

GType gst_hailo_buffer_flag_meta_api_get_type(void)
{
    // https://github.com/vmware/open-vm-tools/commit/b2c8baeaa8ac365e1445f941cf1b80999ed89a9d
    static GType type;
    static const gchar *tags[] = {HAILO_BUFFER_FLAG_META_TAG, NULL};

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register(HAILO_BUFFER_FLAG_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

gboolean gst_hailo_buffer_flag_meta_init(GstMeta *meta, gpointer /*params*/, GstBuffer */*buffer*/)
{
    GstHailoBufferFlagMeta *hailo_buffer_flag_meta = (GstHailoBufferFlagMeta *)meta;
    hailo_buffer_flag_meta->flag = BUFFER_FLAG_NONE;
    return TRUE;
}

void gst_hailo_buffer_flag_meta_free(GstMeta */*meta*/, GstBuffer */*buffer*/)
{}

gboolean gst_hailo_buffer_flag_meta_transform(GstBuffer *dest_buf, GstMeta *src_meta, GstBuffer */*src_buf*/, GQuark /*type*/, gpointer /*data*/)
{
    g_return_val_if_fail(gst_buffer_is_writable(dest_buf), FALSE);

    GstHailoBufferFlagMeta *dst = GST_HAILO_BUFFER_FLAG_META_ADD(dest_buf);
    GstHailoBufferFlagMeta *src = (GstHailoBufferFlagMeta *)src_meta;

    dst->flag = src->flag;
    return TRUE;
}

const GstMetaInfo *gst_hailo_buffer_flag_meta_get_info(void)
{
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *meta = gst_meta_register(
            gst_hailo_buffer_flag_meta_api_get_type(), HAILO_BUFFER_FLAG_META_IMPL_NAME, sizeof(GstHailoBufferFlagMeta),
            (GstMetaInitFunction)gst_hailo_buffer_flag_meta_init, (GstMetaFreeFunction)gst_hailo_buffer_flag_meta_free,
            (GstMetaTransformFunction)gst_hailo_buffer_flag_meta_transform);
        g_once_init_leave(&meta_info, meta);
    }
    return meta_info;
}