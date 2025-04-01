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
#include "sync_gsthailonet.hpp"
#include "sync_gst_hailosend.hpp"
#include "sync_gst_hailorecv.hpp"
#include "gsthailonet.hpp"
#include "gsthailodevicestats.hpp"
#include "tensor_meta.hpp"

static gboolean plugin_init(GstPlugin *plugin)
{
    (void)gst_tensor_meta_get_info();
    (void)gst_tensor_meta_api_get_type();

    return gst_element_register(plugin, "synchailonet", GST_RANK_PRIMARY, GST_TYPE_SYNC_HAILONET) &&
        gst_element_register(plugin, "hailodevicestats", GST_RANK_PRIMARY, GST_TYPE_HAILODEVICESTATS) &&
        gst_element_register(nullptr, "hailosend", GST_RANK_PRIMARY, GST_TYPE_HAILOSEND) &&
        gst_element_register(nullptr, "hailorecv", GST_RANK_PRIMARY, GST_TYPE_HAILORECV) &&
        gst_element_register(plugin, "hailonet", GST_RANK_PRIMARY, GST_TYPE_HAILONET);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, hailo, "hailo gstreamer plugin", plugin_init, VERSION,
    GST_LICENSE_UNKNOWN, "GStreamer", "http://gstreamer.net/")
