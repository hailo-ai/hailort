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
#include "hailo_events.hpp"

GstEvent *HailoSetOutputFormatEvent::build(std::vector<hailo_format_with_name_t> &&formats)
{
    GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, &formats[0], sizeof(formats[0]) * formats.size(), 1);
    GstStructure *str = gst_structure_new(HailoSetOutputFormatEvent::name,
        "formats", G_TYPE_VARIANT, v,
        NULL);
    return gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, str);
}

Expected<HailoSetOutputFormatEvent> HailoSetOutputFormatEvent::parse(GstEvent *event)
{
    if (GST_EVENT_CUSTOM_UPSTREAM != GST_EVENT_TYPE(event)) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    if (!gst_event_has_name(event, HailoSetOutputFormatEvent::name)) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    const GstStructure *str = gst_event_get_structure(event);
    const GValue *f = gst_structure_get_value(str, "formats");
    if (nullptr == f) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
    GVariant *v = g_value_get_variant(f);

    gsize nbytes = 0;
    const hailo_format_with_name_t *formats = reinterpret_cast<const hailo_format_with_name_t*>(g_variant_get_fixed_array(v, &nbytes, 1));
    size_t num_of_formats = nbytes / sizeof(*formats);

    HailoSetOutputFormatEvent event_data = {};
    event_data.formats.reserve(num_of_formats);

    for (uint32_t i = 0; i < num_of_formats; i++) {
        event_data.formats.emplace_back(formats[i]);
    }

    return event_data;
}