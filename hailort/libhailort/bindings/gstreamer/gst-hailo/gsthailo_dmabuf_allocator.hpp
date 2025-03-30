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
#ifndef _GST_HAILO_DMABUF_ALLOCATOR_HPP_
#define _GST_HAILO_DMABUF_ALLOCATOR_HPP_

#include "common.hpp"
#include "hailo/hailort_dma-heap.h"

#include <gst/allocators/gstdmabuf.h>

using namespace hailort;

G_BEGIN_DECLS

#define GST_TYPE_HAILO_DMABUF_ALLOCATOR (gst_hailo_dmabuf_allocator_get_type())
#define GST_HAILO_DMABUF_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HAILO_DMABUF_ALLOCATOR, GstHailoDmabufAllocator))
#define GST_HAILO_DMABUF_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HAILO_DMABUF_ALLOCATOR, GstHailoDmabufAllocator))
#define GST_IS_HAILO_DMABUF_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HAILO_DMABUF_ALLOCATOR))
#define GST_IS_HAILO_DMABUF_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HAILO_DMABUF_ALLOCATOR))

class GstHailoDmaHeapControl {
public:
    static bool dma_heap_fd_open;
    static int dma_heap_fd;
};

struct GstHailoDmabufAllocator
{
    GstDmaBufAllocator parent;
    std::unordered_map<GstMemory*, dma_heap_allocation_data> *dma_buffers;
};

struct GstHailoDmabufAllocatorClass
{
    GstDmaBufAllocatorClass parent;
};

GType gst_hailo_dmabuf_allocator_get_type(void);

G_END_DECLS

#endif /* _GST_HAILO_DMABUF_ALLOCATOR_HPP_ */