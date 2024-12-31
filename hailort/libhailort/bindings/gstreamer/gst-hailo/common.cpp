/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
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
#include "common.hpp"

template<>
HailoElemProperty<gchar*>::~HailoElemProperty()
{
    if (nullptr != m_value) {
        g_free(m_value);
    }
}

GType gst_scheduling_algorithm_get_type (void)
{
    static GType scheduling_algorithm_type = 0;

    /* Tightly coupled to hailo_scheduling_algorithm_e */

    if (!scheduling_algorithm_type) {
        static GEnumValue algorithm_types[] = {
            { HAILO_SCHEDULING_ALGORITHM_NONE,         "Scheduler is not active", "HAILO_SCHEDULING_ALGORITHM_NONE" },
            { HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN,  "Round robin",             "HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN" },
            { HAILO_SCHEDULING_ALGORITHM_MAX_ENUM,     NULL,                      NULL },
        };

        scheduling_algorithm_type =
            g_enum_register_static ("GstHailoSchedulingAlgorithms", algorithm_types);
    }

    return scheduling_algorithm_type;
}

GType gst_hailo_format_type_get_type (void)
{
    static GType format_type_enum = 0;

    /* Tightly coupled to hailo_format_type_t */

    if (!format_type_enum) {
        static GEnumValue format_types[] = {
            { HAILO_FORMAT_TYPE_AUTO,     "auto",     "HAILO_FORMAT_TYPE_AUTO"},
            { HAILO_FORMAT_TYPE_UINT8,    "uint8",    "HAILO_FORMAT_TYPE_UINT8"},
            { HAILO_FORMAT_TYPE_UINT16,   "uint16",   "HAILO_FORMAT_TYPE_UINT16"},
            { HAILO_FORMAT_TYPE_FLOAT32,  "float32",  "HAILO_FORMAT_TYPE_FLOAT32"},
            { HAILO_FORMAT_TYPE_MAX_ENUM,  NULL,      NULL },
        };

        format_type_enum = g_enum_register_static ("GstHailoFormatTypes", format_types);
    }

    return format_type_enum;
}

bool do_versions_match(GstElement *self)
{
    hailo_version_t libhailort_version = {};
    auto status = hailo_get_library_version(&libhailort_version);
    if (HAILO_SUCCESS != status) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Fetching libhailort version has failed! status = %d", status), (NULL));
        return false;
    }

    bool versions_match = ((HAILORT_MAJOR_VERSION == libhailort_version.major) &&
        (HAILORT_MINOR_VERSION == libhailort_version.minor) &&
        (HAILORT_REVISION_VERSION == libhailort_version.revision));
    if (!versions_match) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("libhailort version (%d.%d.%d) does not match gsthailonet version (%d.%d.%d)",
            libhailort_version.major, libhailort_version.minor, libhailort_version.revision,
            HAILORT_MAJOR_VERSION, HAILORT_MINOR_VERSION, HAILORT_REVISION_VERSION), (NULL));
        return false;
    }
    return true;
}