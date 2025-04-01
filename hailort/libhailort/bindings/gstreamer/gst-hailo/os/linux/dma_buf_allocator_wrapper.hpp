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
#ifndef _HAILO_DMABUF_ALLOCATOR_HPP_
#define _HAILO_DMABUF_ALLOCATOR_HPP_

#include "../../common.hpp"
#include "../../gsthailo_dmabuf_allocator.hpp"

class HailoDmaBuffAllocator{
public:
    static Expected<std::shared_ptr<HailoDmaBuffAllocator>> create(gchar *name);
    hailo_status close_dma_heap_fd();
    static Expected<bool> is_dma_buf_memory(GstMapInfo &info);
    static Expected<int> memory_get_fd(GstMapInfo &info);

    GstHailoDmabufAllocator *impl;
};

#endif /* _HAILO_DMABUF_ALLOCATOR_HPP_ */

