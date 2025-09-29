/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_perfetto.cpp
 * @brief Implementation of the hailort perfetto macros
 **/

#include "hailort_perfetto.hpp"


HAILO_PERFETTO_INITIALIZER(hailort_perfetto,
    HAILORT_LIBRARY_TRACK,
    PIPELINE_TRACK,
    ASYNC_INFER_TRACK,
    SCHEDULER_TRACK);
