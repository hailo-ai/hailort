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
#ifndef _GST_HAILO_EVENTS_HPP_
#define _GST_HAILO_EVENTS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "include/hailo_gst.h"

#include <vector>

using namespace hailort;

struct hailo_format_with_name_t {
    hailo_format_t format;
    char name[HAILO_MAX_STREAM_NAME_SIZE];
};

// Event to set the output format of the output layers.
// This event should be sent before the pipeline changes from READY to PAUSED.
struct HailoSetOutputFormatEvent {
    std::vector<hailo_format_with_name_t> formats;

    static constexpr const gchar *name = "HailoSetOutputFormatEvent";
    static GstEvent *build(std::vector<hailo_format_with_name_t> &&formats);
    static Expected<HailoSetOutputFormatEvent> parse(GstEvent *event);
};

#endif /* _GST_HAILO_EVENTS_HPP_ */