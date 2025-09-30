/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_perfetto.hpp
 * @brief Hailo Perfetto macros
 **/
#ifndef _HAILORT_PERFETTO_H_
#define _HAILORT_PERFETTO_H_

#ifdef HAVE_PERFETTO

#include <hailo_perfetto.h>

#define HAILORT_LIBRARY_CATEGORY "hailort_library"
#define HAILORT_LIBRARY_DETAILED_CATEGORY "hailort_library_detailed"

HAILO_PERFETTO_DEFINE_CATEGORIES(hailort_perfetto,
    perfetto::Category(HAILORT_LIBRARY_CATEGORY).SetTags("hailo").SetDescription("Events from hailort_library sub system"),
    perfetto::Category(HAILORT_LIBRARY_DETAILED_CATEGORY).SetTags("hailo").SetDescription("Detailed events from hailort_library sub system"));

/* all tracks that are used as parent tracks have to be registered in hailort_perfetto.cpp HAILO_PERFETTO_INITIALIZER macro*/
#define HAILORT_LIBRARY_TRACK (perfetto::NamedTrack("HailoRT Library", 0))
#define PIPELINE_TRACK (perfetto::NamedTrack("Pipeline", 0, HAILORT_LIBRARY_TRACK))
#define ASYNC_INFER_TRACK (perfetto::NamedTrack("Async Infer", 0, HAILORT_LIBRARY_TRACK))
#define SCHEDULER_TRACK (perfetto::NamedTrack("Scheduler", 0, HAILORT_LIBRARY_TRACK))

#define HAILORT_LIBRARY_TRACE_EVENT(event_name, track, category, ...) TRACE_EVENT((category), (event_name), (track), ##__VA_ARGS__)

#define HAILORT_LIBRARY_TRACE_EVENT_BEGIN(event_name, track, category, ...)                                                       \
    TRACE_EVENT_BEGIN((category), (event_name), (track), ##__VA_ARGS__)
#define HAILORT_LIBRARY_TRACE_EVENT_END(track, category) TRACE_EVENT_END((category), (track))

/* async event API - will create a dedicated track for this async event. event_name has to match between _BEGIN and _END
 */
 #define HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(event_name, id, parent_track, category, ...)                                      \
    HAILORT_LIBRARY_TRACE_EVENT_BEGIN((event_name), perfetto::NamedTrack((event_name), (id), (parent_track)), (category), ##__VA_ARGS__)
#define HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(event_name, id, parent_track, category)                                        \
    HAILORT_LIBRARY_TRACE_EVENT_END(perfetto::NamedTrack((event_name), (id), (parent_track)), (category))

#else // no HAVE_PERFETTO

/* assert either HAVE_PERFETTO or PERFETTO_DISABLED is defined to avoid cmake bugs */
#ifndef PERFETTO_DISABLED
#error "Perfetto define not found - probably target is missing cmake common_args"
#endif // no PERFETTO_DISABLED

/* no perfetto - empty macros */
#define HAILORT_LIBRARY_TRACE_EVENT_BEGIN(...)
#define HAILORT_LIBRARY_TRACE_EVENT_END(...)
#define HAILORT_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(...)
#define HAILORT_LIBRARY_TRACE_ASYNC_EVENT_END(...)

#endif // HAVE_PERFETTO


#endif // _HAILORT_PERFETTO_H_
