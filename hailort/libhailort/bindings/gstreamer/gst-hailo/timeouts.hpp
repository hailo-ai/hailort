/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL-2.1-or-later license (https://spdx.org/licenses/LGPL-2.1-or-later.html)
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
#ifndef _GST_HAILO_TIMEOUTS_HPP_
#define _GST_HAILO_TIMEOUTS_HPP_

#include <chrono>
#include <cstdint>

// --- Scheduler ---
constexpr uint32_t HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS = 0;

// --- Async infer ---
constexpr std::chrono::milliseconds WAIT_FOR_ASYNC_READY_TIMEOUT(10000);

#endif /* _GST_HAILO_TIMEOUTS_HPP_ */
